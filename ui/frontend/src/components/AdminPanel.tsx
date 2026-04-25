import { useState, useEffect, useRef, useCallback, useMemo, type ReactNode } from "react";
import type { DatasetInfo } from "../types";

// Matches /api/datasets/{dataset}/services?fields=detail response
interface ServiceEntry {
  id: string;
  name: string;
  description: string;
  source: "user_config" | "api_config" | "ephemeral" | "skill_folder";
}

type OpType = "register" | "deregister" | "build" | "query";
type RegType = "generic" | "a2a" | "skill";
type A2AInputMode = "url" | "json";
type QueryMode = "list" | "single";

interface SkillPreview {
  name: string;
  description: string;
  files: string[];
  totalSize: number;
}

// ── Dataset-specific default values ──────────────────────────────────────────

interface DatasetDefaults {
  generic: { name: string; description: string; metadata: string };
  a2a_url: { agentCardUrl: string };
  a2a_json: { agentCardJson: string };
}

const DATASET_DEFAULTS: Record<string, DatasetDefaults> = {
  default: {
    generic: {
      name: "天气查询服务",
      description: "根据城市名称查询实时天气信息，返回当前温度、湿度、天气状况及未来24小时预报。",
      metadata: JSON.stringify(
        {
          url: "https://api.example.com/weather",
          inputSchema: { type: "object", properties: { city: { type: "string" } }, required: ["city"] },
        },
        null,
        2,
      ),
    },
    a2a_url: {
      agentCardUrl: "https://hello-world-gxfr.onrender.com/.well-known/agent.json",
    },
    a2a_json: {
      agentCardJson: JSON.stringify(
        {
          name: "智能助手 Demo",
          description: "多功能 AI 助手，支持网页搜索、文本摘要和信息问答，适合复杂任务的自动化处理。",
          url: "https://demo-agent.example.com/a2a",
          version: "1.0.0",
          skills: [
            { id: "web_search", name: "网页搜索", description: "搜索互联网获取最新信息" },
            { id: "summarize", name: "文本摘要", description: "对长文本进行摘要提炼" },
          ],
        },
        null,
        2,
      ),
    },
  },
};

// ── Request preview builder ───────────────────────────────────────────────────

interface PreviewStep {
  method: "POST" | "DELETE" | "GET";
  path: string;
  body: Record<string, unknown> | null;
}

type PreviewInfo = PreviewStep[];

function buildPreview(
  op: OpType,
  regType: RegType,
  a2aMode: A2AInputMode,
  queryMode: QueryMode,
  dataset: string,
  fields: Record<string, string>,
  persistent: boolean,
  resume: string,
  defs: DatasetDefaults | null,
  isNewDataset: boolean,
  newEmbeddingModel: string,
  showBuildLogs: boolean,
  skillPreview: SkillPreview | null,
): PreviewInfo {
  const ds = dataset || "<dataset>";
  const createStep: PreviewStep | null = isNewDataset
    ? { method: "POST", path: "/api/datasets", body: { name: ds, embedding_model: newEmbeddingModel } }
    : null;

  if (op === "register") {
    let regStep: PreviewStep;
    if (regType === "generic") {
      const body: Record<string, unknown> = {};
      if (fields.serviceId) body.service_id = fields.serviceId;
      body.name = fields.name || defs?.generic.name || "<name>";
      body.description = fields.description || defs?.generic.description || "<description>";
      const metaStr = fields.metadata || defs?.generic.metadata;
      if (metaStr) {
        try {
          const meta = JSON.parse(metaStr);
          if (meta.url) body.url = meta.url;
          if (meta.inputSchema) body.inputSchema = meta.inputSchema;
        } catch {
          body._metadata = "<invalid JSON>";
        }
      }
      body.persistent = persistent;
      regStep = { method: "POST", path: `/api/datasets/${ds}/services/generic`, body };
    } else if (regType === "a2a") {
      const body: Record<string, unknown> = {};
      if (fields.serviceId) body.service_id = fields.serviceId;
      if (a2aMode === "url") {
        body.agent_card_url = fields.agentCardUrl || defs?.a2a_url.agentCardUrl || "<agent_card_url>";
      } else {
        const cardStr = fields.agentCardJson || defs?.a2a_json.agentCardJson;
        if (cardStr) {
          try { body.agent_card = JSON.parse(cardStr); }
          catch { body.agent_card = "<invalid JSON>"; }
        } else {
          body.agent_card = "<agent_card>";
        }
      }
      body.persistent = persistent;
      regStep = { method: "POST", path: `/api/datasets/${ds}/services/a2a`, body };
    } else {
      // skill
      regStep = {
        method: "POST", path: `/api/datasets/${ds}/skills`,
        body: { _note: "multipart/form-data (ZIP)", files: skillPreview?.files || [] },
      };
    }
    return createStep ? [createStep, regStep] : [regStep];
  }

  if (op === "deregister") {
    // Skill entries use a different delete endpoint (by name, not service_id)
    if (fields.deregSkillName) {
      return [{
        method: "DELETE",
        path: `/api/datasets/${ds}/skills/${encodeURIComponent(fields.deregSkillName)}`,
        body: null,
      }];
    }
    return [{
      method: "DELETE",
      path: `/api/datasets/${ds}/services/${fields.serviceId || "<service_id>"}`,
      body: null,
    }];
  }

  if (op === "query") {
    if (queryMode === "single") {
      return [{
        method: "GET",
        path: `/api/datasets/${ds}/services/${encodeURIComponent(fields.serviceId || "<id>")}`,
        body: null,
      }];
    }
    const params = new URLSearchParams();
    params.set("fields", "detail");
    params.set("size", fields.listSize || "-1");
    params.set("page", fields.listPage || "1");
    return [{
      method: "GET",
      path: `/api/datasets/${ds}/services?${params.toString()}`,
      body: null,
    }];
  }

  // build
  const buildBody: Record<string, unknown> = {};
  if (resume !== "no") buildBody.resume = resume;
  if (fields.genericRatio)       buildBody.generic_ratio        = parseFloat(fields.genericRatio);
  if (fields.deleteThreshold)    buildBody.delete_threshold     = parseInt(fields.deleteThreshold);
  if (fields.maxServiceSize)     buildBody.max_service_size     = parseInt(fields.maxServiceSize);
  if (fields.maxCategoriesSize)  buildBody.max_categories_size  = parseInt(fields.maxCategoriesSize);
  if (fields.maxDepth)           buildBody.max_depth            = parseInt(fields.maxDepth);
  if (fields.minLeafSize)        buildBody.min_leaf_size        = parseInt(fields.minLeafSize);
  if (fields.keywordBatchSize)   buildBody.keyword_batch_size       = parseInt(fields.keywordBatchSize);
  if (fields.maxKeywordsPerSvc)  buildBody.max_keywords_per_service = parseInt(fields.maxKeywordsPerSvc);
  if (fields.keywordThreshold)   buildBody.keyword_threshold        = parseInt(fields.keywordThreshold);
  if (fields.classRetries)       buildBody.classification_retries  = parseInt(fields.classRetries);
  if (fields.maxRefineIter)      buildBody.max_refine_iterations   = parseInt(fields.maxRefineIter);
  if (fields.tempKeywords)       buildBody.temperature_keywords = parseFloat(fields.tempKeywords);
  if (fields.tempDesign)         buildBody.temperature_design   = parseFloat(fields.tempDesign);
  if (fields.tempClassify)       buildBody.temperature_classify = parseFloat(fields.tempClassify);
  if (fields.maxTokDesign)       buildBody.max_tokens_design       = parseInt(fields.maxTokDesign);
  if (fields.maxTokDesignSmall)  buildBody.max_tokens_design_small = parseInt(fields.maxTokDesignSmall);
  if (fields.maxTokClassify)     buildBody.max_tokens_classify     = parseInt(fields.maxTokClassify);
  if (fields.maxTokValidate)     buildBody.max_tokens_validate     = parseInt(fields.maxTokValidate);
  if (fields.maxTokKeywords)     buildBody.max_tokens_keywords     = parseInt(fields.maxTokKeywords);
  if (fields.buildWorkers)       buildBody.workers             = parseInt(fields.buildWorkers);
  if (showBuildLogs) buildBody.log_level = "INFO";
  return [{ method: "POST", path: `/api/datasets/${ds}/build`, body: buildBody }];
}

// ── Types ─────────────────────────────────────────────────────────────────────

interface ResponseData {
  status: number;
  body: unknown;
  elapsed: number;
  pagination?: { page: number; total_pages: number; total_count: number; page_size: number };
}

const BUILD_MODES = [
  { val: "no",  label: "完整重建", desc: "从头开始构建全新的分类树" },
  { val: "yes", label: "智能续建", desc: "跳过已完成步骤，断点续建" },
] as const;

// ── Component ─────────────────────────────────────────────────────────────────

export default function AdminPanel() {
  const [datasets, setDatasets] = useState<DatasetInfo[]>([]);
  const [op, setOp] = useState<OpType>("register");
  const [regType, setRegType] = useState<RegType>("generic");
  const [a2aMode, setA2AMode] = useState<A2AInputMode>("url");
  const [queryMode, setQueryMode] = useState<QueryMode>("list");
  const [skillFiles, setSkillFiles] = useState<File[]>([]);
  const [skillPreview, setSkillPreview] = useState<SkillPreview | null>(null);
  const [dataset, setDataset] = useState("");
  const [newDatasetName, setNewDatasetName] = useState("");
  const [useNew, setUseNew] = useState(false);
  const [fields, setFields] = useState<Record<string, string>>({
    // Register
    serviceId: "", name: "", description: "", metadata: "",
    agentCardUrl: "", agentCardJson: "",
    // Build — core
    genericRatio: "", deleteThreshold: "", maxServiceSize: "", maxCategoriesSize: "",
    maxDepth: "", minLeafSize: "",
    // Build — keyword
    keywordBatchSize: "", maxKeywordsPerSvc: "", keywordThreshold: "",
    // Build — iteration
    classRetries: "", maxRefineIter: "",
    // Build — temperature
    tempKeywords: "", tempDesign: "", tempClassify: "",
    // Build — token limits
    maxTokDesign: "", maxTokDesignSmall: "", maxTokClassify: "", maxTokValidate: "", maxTokKeywords: "",
    // Build — workers
    buildWorkers: "",
    // List
    listSize: "-1", listPage: "1",
  });
  const [persistent, setPersistent] = useState(true);
  const [resume, setResume] = useState("no");
  const [loading, setLoading] = useState(false);
  const [isPolling, setIsPolling] = useState(false);
  const [response, setResponse] = useState<ResponseData | null>(null);
  const [reqError, setReqError] = useState<string | null>(null);
  const [browserRefreshKey, setBrowserRefreshKey] = useState(0);
  const [showBuildLogs, setShowBuildLogs] = useState(true);
  const [splitPct, setSplitPct] = useState(60);
  const [embeddingModels, setEmbeddingModels] = useState<Record<string, { dim: number; language: string; description: string }>>({});
  const [embeddingModel, setEmbeddingModel] = useState("");
  const [datasetEmbeddingModel, setDatasetEmbeddingModel] = useState("");  // current saved config
  const esRef = useRef<EventSource | null>(null);
  const streamLogsRef = useRef<string[]>([]);
  const startRef = useRef<number>(0);
  const panelRef = useRef<HTMLDivElement>(null);
  const logEndRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    fetch("/api/datasets")
      .then((r) => r.json())
      .then((list: DatasetInfo[]) => {
        setDatasets(list);
        if (list.length > 0) setDataset(list[0].name);
      })
      .catch(console.error);
    fetch("/api/datasets/embedding-models")
      .then((r) => r.json())
      .then((data) => setEmbeddingModels(data.models || {}))
      .catch(console.error);
  }, []);

  // Load vector config when dataset changes
  useEffect(() => {
    if (!dataset) return;
    fetch(`/api/datasets/${encodeURIComponent(dataset)}/vector-config`)
      .then((r) => r.json())
      .then((cfg) => {
        const m = cfg.embedding_model || "";
        setEmbeddingModel(m);
        setDatasetEmbeddingModel(m);
      })
      .catch(console.error);
  }, [dataset]);

  // Cleanup: close SSE stream on unmount
  useEffect(() => () => {
    if (esRef.current) { esRef.current.close(); esRef.current = null; }
  }, []);

  // Open SSE stream for a specific dataset; replays history then receives live events
  const openStream = useCallback((ds: string) => {
    if (esRef.current) { esRef.current.close(); esRef.current = null; }
    streamLogsRef.current = [];
    startRef.current = Date.now();
    setIsPolling(true);
    setShowBuildLogs(true);
    setResponse(null);

    const es = new EventSource(`/api/datasets/${encodeURIComponent(ds)}/build/stream`);
    esRef.current = es;

    es.onmessage = (e) => {
      try {
        const item = JSON.parse(e.data) as { type: string; message?: string; status?: string };
        if (item.type === "log") {
          streamLogsRef.current = [...streamLogsRef.current, item.message!];
          setResponse({
            status: 200,
            body: { dataset: ds, status: "running", logs: streamLogsRef.current },
            elapsed: (Date.now() - startRef.current) / 1000,
          });
        } else if (item.type === "status") {
          setResponse({
            status: 200,
            body: { dataset: ds, status: item.status, message: item.message, logs: streamLogsRef.current },
            elapsed: (Date.now() - startRef.current) / 1000,
          });
          es.close();
          esRef.current = null;
          setIsPolling(false);
        }
      } catch { /* ignore malformed messages */ }
    };

    es.onerror = () => {
      es.close();
      esRef.current = null;
      setIsPolling(false);
    };
  }, []);

  // Close stream and clear response when switching datasets (each dataset is independent)
  useEffect(() => {
    if (esRef.current) { esRef.current.close(); esRef.current = null; setIsPolling(false); }
    streamLogsRef.current = [];
    setResponse(null);
    setShowBuildLogs(false);
  }, [dataset]);

  // On dataset load: if a build is already running, auto-switch to build tab
  useEffect(() => {
    if (!dataset) return;
    fetch(`/api/datasets/${encodeURIComponent(dataset)}/build/status`)
      .then(r => r.json())
      .then(data => { if (data.status === "running") setOp("build"); })
      .catch(() => {});
  }, [dataset]);

  // Auto-connect stream when entering build tab or switching to a building dataset
  useEffect(() => {
    if (op !== "build" || !dataset || esRef.current !== null) return;
    const ds = dataset;
    fetch(`/api/datasets/${encodeURIComponent(ds)}/build/status`)
      .then(r => r.json())
      .then(data => { if (data.status === "running") openStream(ds); })
      .catch(() => {});
  }, [op, dataset, openStream]);

  useEffect(() => {
    if (showBuildLogs && isPolling) logEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [response, showBuildLogs, isPolling]);

  const refreshDatasets = useCallback(() => {
    fetch("/api/datasets")
      .then((r) => r.json())
      .then((list: DatasetInfo[]) => setDatasets(list))
      .catch(console.error);
  }, []);

  const handleDragStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const container = panelRef.current;
    if (!container) return;
    const onMove = (ev: MouseEvent) => {
      const rect = container.getBoundingClientRect();
      const pct = ((ev.clientY - rect.top) / rect.height) * 100;
      setSplitPct(Math.max(20, Math.min(80, pct)));
    };
    const onUp = () => {
      document.removeEventListener("mousemove", onMove);
      document.removeEventListener("mouseup", onUp);
    };
    document.addEventListener("mousemove", onMove);
    document.addEventListener("mouseup", onUp);
  }, []);

  const activeDataset = useNew ? newDatasetName.trim() : dataset;
  const defs = DATASET_DEFAULTS[activeDataset] ?? null;

  const preview = buildPreview(op, regType, a2aMode, queryMode, activeDataset, fields, persistent, resume, defs, useNew, embeddingModel, showBuildLogs, skillPreview);

  const setField = (key: string, val: string) =>
    setFields((prev) => ({ ...prev, [key]: val }));

  const changeOp = useCallback((o: OpType) => {
    if (esRef.current) { esRef.current.close(); esRef.current = null; setIsPolling(false); }
    setOp(o);
    setUseNew(false);
    setResponse(null);
    setReqError(null);
    // Clear operation-specific fields to avoid stale values leaking across tabs
    setFields((prev) => ({
      ...prev,
      serviceId: "", name: "", description: "", metadata: "",
      agentCardUrl: "", agentCardJson: "", deregSkillName: "",
    }));
  }, []);

  const handleSubmit = useCallback(async () => {
    if (esRef.current) { esRef.current.close(); esRef.current = null; setIsPolling(false); }
    setLoading(true);
    setResponse(null);
    setReqError(null);
    startRef.current = Date.now();

    try {
      // Skill registration: uses FormData, not JSON preview steps
      if (op === "register" && regType === "skill" && skillFiles.length > 0 && skillPreview) {
        const JSZip = (await import("jszip")).default;
        const zip = new JSZip();
        for (const file of skillFiles) {
          const relPath = file.webkitRelativePath.split("/").slice(1).join("/");
          if (relPath) zip.file(relPath, file);
        }
        const blob = await zip.generateAsync({ type: "blob" });
        const formData = new FormData();
        formData.append("file", blob, `${skillPreview.name}.zip`);

        // If new dataset, create it first
        if (useNew) {
          const createResp = await fetch("/api/datasets", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ name: activeDataset, embedding_model: embeddingModel }),
          });
          if (!createResp.ok) {
            const body = await createResp.json();
            setResponse({ status: createResp.status, body, elapsed: (Date.now() - startRef.current) / 1000 });
            setLoading(false);
            return;
          }
        }

        const resp = await fetch(`/api/datasets/${encodeURIComponent(activeDataset)}/skills`, {
          method: "POST", body: formData,
        });
        const body = await resp.json();
        setResponse({ status: resp.status, body, elapsed: (Date.now() - startRef.current) / 1000 });
        setLoading(false);
        if (resp.ok) {
          refreshDatasets();
          if (useNew) { setUseNew(false); setDataset(activeDataset); }
        }
        return;
      }

      // Single query: response may be ZIP (for skill) instead of JSON
      if (op === "query" && queryMode === "single" && fields.serviceId) {
        const url = `/api/datasets/${encodeURIComponent(activeDataset)}/services/${encodeURIComponent(fields.serviceId)}`;
        const resp = await fetch(url);
        const contentType = resp.headers.get("Content-Type") || "";
        if (contentType.includes("application/zip")) {
          const blob = await resp.blob();
          const a = document.createElement("a");
          a.href = URL.createObjectURL(blob);
          a.download = `${fields.serviceId}.zip`;
          a.click();
          URL.revokeObjectURL(a.href);
          setResponse({ status: resp.status, body: { message: "Skill ZIP 已下载" }, elapsed: (Date.now() - startRef.current) / 1000 });
        } else {
          const body = await resp.json();
          setResponse({ status: resp.status, body, elapsed: (Date.now() - startRef.current) / 1000 });
        }
        setLoading(false);
        return;
      }

      // Execute all preview steps sequentially
      let lastBody: unknown = null;
      let lastStatus = 200;
      let lastPagination: ResponseData["pagination"];
      for (const step of preview) {
        const resp = await fetch(step.path, {
          method: step.method,
          ...(step.body !== null
            ? { headers: { "Content-Type": "application/json" }, body: JSON.stringify(step.body) }
            : {}),
        });
        lastBody = await resp.json();
        lastStatus = resp.status;
        const total = resp.headers.get("X-Total-Count");
        const pg = resp.headers.get("X-Page");
        const tp = resp.headers.get("X-Total-Pages");
        const ps = resp.headers.get("X-Page-Size");
        lastPagination = total && pg && tp && ps
          ? { page: +pg, total_pages: +tp, total_count: +total, page_size: +ps }
          : undefined;
        if (!resp.ok) break;
      }
      setResponse({ status: lastStatus, body: lastBody, elapsed: (Date.now() - startRef.current) / 1000, pagination: lastPagination });
      setLoading(false);

      if ((op === "register" || op === "deregister") && lastStatus < 400) {
        // After deregister, check if dataset is now empty → auto-delete
        if (op === "deregister") {
          setBrowserRefreshKey((k) => k + 1);
          try {
            const svcResp = await fetch(`/api/datasets/${encodeURIComponent(activeDataset)}/services?fields=brief`);
            const svcs = await svcResp.json();
            if (Array.isArray(svcs) && svcs.length === 0) {
              await fetch(`/api/datasets/${encodeURIComponent(activeDataset)}`, { method: "DELETE" });
            }
          } catch { /* best effort */ }
        }
        refreshDatasets();
        if (useNew) { setUseNew(false); setDataset(activeDataset); }
      }

      if (op === "build" && lastStatus < 400 && showBuildLogs) {
        openStream(activeDataset);
      }
    } catch (e) {
      setLoading(false);
      setReqError(String(e));
    }
  }, [preview, op, activeDataset, refreshDatasets, openStream, useNew, showBuildLogs, regType, skillFiles, skillPreview, embeddingModel, queryMode, fields.serviceId]);

  const handleCancelBuild = useCallback(async () => {
    if (!activeDataset) return;
    try {
      await fetch(`/api/datasets/${encodeURIComponent(activeDataset)}/build`, { method: "DELETE" });
    } catch { /* SSE stream will close on its own when status event arrives */ }
  }, [activeDataset]);

  // canSubmit: check effective values (field || default)
  const canSubmit = !loading && !!activeDataset && (() => {
    if (op === "deregister") return !!fields.serviceId;
    if (op === "build") return true;
    if (op === "query") return queryMode === "list" || !!fields.serviceId;
    // register
    if (regType === "generic") {
      return !!(fields.name || defs?.generic.name) && !!(fields.description || defs?.generic.description);
    }
    if (regType === "skill") return skillFiles.length > 0 && !!skillPreview?.name;
    // a2a
    if (a2aMode === "url") return !!(fields.agentCardUrl || defs?.a2a_url.agentCardUrl);
    return !!(fields.agentCardJson || defs?.a2a_json.agentCardJson);
  })();

  const lastStep = preview[preview.length - 1];
  const bodyStr = preview.map((s) => s.body !== null ? JSON.stringify(s.body, null, 2) : "(no body)").join("\n---\n");
  const responseBodyStr = response?.body !== undefined ? JSON.stringify(response.body, null, 2) : "";
  const respStatus =
    response?.body && typeof response.body === "object" && response.body !== null && "status" in (response.body as object)
      ? (response.body as { status: string }).status
      : null;

  return (
    <div className="flex-1 min-w-0 flex overflow-hidden">
      {/* ── LEFT: operation panel ── */}
      <aside className="w-2/5 shrink-0 bg-white border-r border-zinc-200/80 flex flex-col">
        <div className="flex-1 flex flex-col px-6 py-5 gap-4 min-h-0">

          {/* 1. Op type */}
          <section className="shrink-0">
            <SLabel n={1} text="操作类型" />
            <div className="mt-2 flex rounded-lg border border-zinc-200 overflow-hidden text-[12px] font-medium shadow-sm">
              {(["register", "deregister", "query", "build"] as const).map((o, i) => (
                <button key={o} onClick={() => changeOp(o)}
                  className={`flex-1 py-1.5 transition-colors ${i > 0 ? "border-l border-zinc-200" : ""} ${
                    op === o ? "bg-zinc-800 text-white" : "bg-white text-zinc-500 hover:bg-zinc-50"
                  }`}>
                  {o === "register" ? "注册" : o === "deregister" ? "注销" : o === "query" ? "查询" : "构建分类树"}
                </button>
              ))}
            </div>
          </section>

          {/* 2. Dataset */}
          <section className="shrink-0">
            <SLabel n={2} text="数据集" />
            <div className="mt-2 space-y-2">
              <div className="flex flex-wrap gap-2">
                {datasets.map((d) => (
                  <button key={d.name}
                    onClick={() => { setDataset(d.name); setUseNew(false); }}
                    className={`px-3 py-1 rounded-md text-[12px] border transition-all ${
                      !useNew && dataset === d.name
                        ? "bg-zinc-800 text-white border-zinc-800"
                        : "bg-white text-zinc-600 border-zinc-200 hover:border-zinc-300"
                    }`}>
                    {d.name}
                    <span className="ml-1.5 text-[10px] text-zinc-400 opacity-70">{d.service_count}</span>
                  </button>
                ))}
                {op === "register" && (
                  <button onClick={() => setUseNew((v) => !v)}
                    className={`px-3 py-1 rounded-md text-[12px] border border-dashed transition-all ${
                      useNew
                        ? "border-zinc-500 text-zinc-700 bg-zinc-50"
                        : "border-zinc-300 text-zinc-400 hover:border-zinc-400 hover:text-zinc-600"
                    }`}>
                    + 新建
                  </button>
                )}
              </div>
              {useNew && op === "register" && (
                <div className="space-y-2">
                  <input autoFocus value={newDatasetName}
                    onChange={(e) => setNewDatasetName(e.target.value)}
                    placeholder="输入新数据集名称..."
                    className="w-full px-3 py-1.5 rounded-md border border-zinc-300 text-[12px] bg-white
                      focus:outline-none focus:ring-1 focus:ring-zinc-400/50 focus:border-zinc-400 placeholder-zinc-300" />
                  <div className="flex items-center gap-2">
                    <span className="text-[11px] text-zinc-400 whitespace-nowrap">Embedding</span>
                    <select value={embeddingModel}
                      onChange={(e) => setEmbeddingModel(e.target.value)}
                      className="flex-1 rounded border border-zinc-200 bg-white px-2 py-1 text-[11px] text-zinc-600
                        focus:ring-1 focus:ring-zinc-400 focus:border-zinc-400">
                      {Object.entries(embeddingModels).map(([name, info]) => (
                        <option key={name} value={name}>{name} — {info.description}</option>
                      ))}
                    </select>
                  </div>
                </div>
              )}
            </div>
          </section>

          {/* 3. Params */}
          <section className="flex-1 min-h-0 flex flex-col">
            <SLabel n={3} text="参数" />
            <div className={`mt-2 flex-1 min-h-0 ${op === "build" ? "overflow-y-auto pr-0.5" : "flex flex-col"}`}>

              {/* ── Register ── */}
              {op === "register" && (
                <div className="flex flex-col flex-1 min-h-0 gap-3">
                  {/* Service type toggle */}
                  <div className="shrink-0 flex rounded-lg border border-zinc-200 overflow-hidden text-[12px] font-medium shadow-sm">
                    {(["generic", "a2a", "skill"] as const).map((t, i) => (
                      <button key={t} onClick={() => { setRegType(t); setSkillFiles([]); setSkillPreview(null); }}
                        className={`flex-1 py-1 transition-colors ${i > 0 ? "border-l border-zinc-200" : ""} ${
                          regType === t ? "bg-zinc-800 text-white" : "bg-white text-zinc-500 hover:bg-zinc-50"
                        }`}>
                        {t === "generic" ? "通用服务" : t === "a2a" ? "A2A Agent" : "Skill"}
                      </button>
                    ))}
                  </div>

                  {regType === "generic" ? (
                    /* ── Generic fields ── */
                    <div className="flex flex-col flex-1 min-h-0 gap-2.5">
                      <Field label="service_id" note="可选，留空自动生成"
                        value={fields.serviceId} onChange={(v) => setField("serviceId", v)} />
                      <Field label="name" required placeholder="服务名称"
                        defaultValue={defs?.generic.name}
                        value={fields.name} onChange={(v) => setField("name", v)} />
                      <Field label="description" required placeholder="服务功能描述"
                        defaultValue={defs?.generic.description}
                        value={fields.description} onChange={(v) => setField("description", v)} textarea rows={3} />
                      <Field label="metadata" note="可选，JSON 格式，含 url / inputSchema 等"
                        placeholder={'{\n  "url": "https://...",\n  "inputSchema": {"type":"object"}\n}'}
                        defaultValue={defs?.generic.metadata}
                        value={fields.metadata} onChange={(v) => setField("metadata", v)} textarea expand mono />
                      <div className="shrink-0">
                        <PersistentToggle value={persistent} onChange={setPersistent} />
                      </div>
                    </div>
                  ) : regType === "a2a" ? (
                    /* ── A2A fields ── */
                    <div className="flex flex-col flex-1 min-h-0 gap-3">
                      {/* A2A input mode toggle */}
                      <div className="shrink-0 flex gap-2 p-1 rounded-lg bg-zinc-100">
                        {(["url", "json"] as const).map((m) => (
                          <button key={m} onClick={() => setA2AMode(m)}
                            className={`flex-1 py-1 rounded-md text-[11px] font-medium transition-all ${
                              a2aMode === m ? "bg-white text-zinc-800 shadow-sm" : "text-zinc-500 hover:text-zinc-700"
                            }`}>
                            {m === "url" ? "URL 模式" : "JSON 模式"}
                          </button>
                        ))}
                      </div>

                      <div className="flex flex-col flex-1 min-h-0 gap-2.5">
                        <Field label="service_id" note="可选，留空自动生成"
                          value={fields.serviceId} onChange={(v) => setField("serviceId", v)} />

                        {a2aMode === "url" ? (
                          <Field
                            label="agent_card_url" required
                            placeholder="https://agent.example.com/.well-known/agent.json"
                            defaultValue={defs?.a2a_url.agentCardUrl}
                            value={fields.agentCardUrl} onChange={(v) => setField("agentCardUrl", v)} />
                        ) : (
                          <Field
                            label="agent_card" required
                            placeholder='{"name":"...","description":"...","url":"...","skills":[]}'
                            defaultValue={defs?.a2a_json.agentCardJson}
                            value={fields.agentCardJson} onChange={(v) => setField("agentCardJson", v)}
                            textarea expand mono />
                        )}

                        <div className="shrink-0">
                          <PersistentToggle value={persistent} onChange={setPersistent} />
                        </div>
                      </div>
                    </div>
                  ) : (
                    /* ── Skill fields ── */
                    <div className="flex flex-col flex-1 min-h-0 gap-3">
                      <div className="shrink-0">
                        <div className="flex items-baseline gap-1 mb-1">
                          <span className="text-[11px] font-medium text-zinc-600">选择 Skill 文件夹</span>
                          <span className="text-[10px] text-red-400">必填</span>
                        </div>
                        <label className="flex items-center gap-2 px-3 py-2 rounded-md border border-dashed border-zinc-300
                          hover:border-zinc-400 cursor-pointer transition-colors bg-white">
                          <svg className="w-4 h-4 text-zinc-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                              d="M3 7v10a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-6l-2-2H5a2 2 0 00-2 2z" />
                          </svg>
                          <span className="text-[12px] text-zinc-500">
                            {skillPreview ? skillPreview.name : "点击选择文件夹..."}
                          </span>
                          <input
                            type="file"
                            {...{ webkitdirectory: "", directory: "" } as any}
                            className="hidden"
                            onChange={(e) => {
                              const files = Array.from(e.target.files || []);
                              if (files.length === 0) return;
                              setSkillFiles(files);
                              // Parse SKILL.md from selected files
                              const skillMd = files.find((f) =>
                                f.webkitRelativePath.split("/").pop() === "SKILL.md"
                                && f.webkitRelativePath.split("/").length === 2
                              );
                              if (!skillMd) {
                                setSkillPreview(null);
                                return;
                              }
                              skillMd.text().then((text) => {
                                const parts = text.split("---");
                                if (parts.length < 3) { setSkillPreview(null); return; }
                                const fm: Record<string, string> = {};
                                for (const raw of parts[1].trim().split("\n")) {
                                  const line = raw.replace(/\r$/, "");
                                  const m = line.match(/^(name|description|license):\s*(.+)$/);
                                  if (m) {
                                    let val = m[2].trim();
                                    if (val.length >= 2 && (val[0] === '"' || val[0] === "'") && val[val.length - 1] === val[0])
                                      val = val.slice(1, -1);
                                    fm[m[1]] = val;
                                  }
                                }
                                if (!fm.name || !fm.description) { setSkillPreview(null); return; }
                                setSkillPreview({
                                  name: fm.name,
                                  description: fm.description,
                                  files: files.map((f) => f.webkitRelativePath.split("/").slice(1).join("/")),
                                  totalSize: files.reduce((s, f) => s + f.size, 0),
                                });
                              });
                            }}
                          />
                        </label>
                      </div>
                      {skillPreview && (
                        <div className="shrink-0 rounded-md border border-zinc-200 bg-zinc-50 p-3 space-y-1.5">
                          <div className="text-[12px] font-medium text-zinc-700">{skillPreview.name}</div>
                          <div className="text-[11px] text-zinc-500 line-clamp-2">{skillPreview.description}</div>
                          <div className="text-[10px] text-zinc-400">
                            {skillPreview.files.length} 个文件 · {(skillPreview.totalSize / 1024).toFixed(1)} KB
                          </div>
                        </div>
                      )}
                      {skillFiles.length > 0 && !skillPreview && (
                        <div className="shrink-0 rounded-md border border-red-200 bg-red-50 px-3 py-2 text-[11px] text-red-600">
                          未找到有效的 SKILL.md（需包含 name 和 description 字段）
                        </div>
                      )}
                    </div>
                  )}
                </div>
              )}

              {/* ── Deregister ── */}
              {op === "deregister" && (
                <div className="flex flex-col flex-1 min-h-0 gap-3">
                  {/* service_id input */}
                  <div className="shrink-0">
                    <div className="flex items-baseline gap-1 mb-1">
                      <span className="text-[11px] font-medium text-zinc-600 font-mono">service_id</span>
                      <span className="text-[10px] text-red-400">必填</span>
                    </div>
                    <div className="flex items-center gap-1.5">
                      <input
                        type="text"
                        value={fields.serviceId}
                        onChange={(e) => setField("serviceId", e.target.value)}
                        placeholder="输入服务 ID，或浏览下方列表选择"
                        className="flex-1 px-3 py-1.5 rounded-md border border-zinc-200 text-[12px] font-mono bg-white
                          focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400 placeholder-zinc-300"
                      />
                      {fields.serviceId && (
                        <button onClick={() => setField("serviceId", "")}
                          className="shrink-0 text-zinc-300 hover:text-zinc-600 transition-colors p-1">
                          <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                          </svg>
                        </button>
                      )}
                    </div>
                  </div>
                  {/* Service browser — fills remaining space */}
                  <RegistryBrowser
                    dataset={activeDataset}
                    selected={fields.serviceId}
                    onSelect={(id, entry) => {
                      setField("serviceId", id);
                      if (entry?.source === "skill_folder") setField("deregSkillName", entry.name);
                      else setField("deregSkillName", "");
                    }}
                    refreshKey={browserRefreshKey}
                  />
                </div>
              )}

              {/* ── Query ── */}
              {op === "query" && (
                <div className="flex flex-col flex-1 min-h-0 gap-3">
                  {/* Sub-mode toggle */}
                  <div className="shrink-0 flex gap-2 p-1 rounded-lg bg-zinc-100">
                    {(["list", "single"] as const).map((m) => (
                      <button key={m} onClick={() => setQueryMode(m)}
                        className={`flex-1 py-1 rounded-md text-[11px] font-medium transition-all ${
                          queryMode === m ? "bg-white text-zinc-800 shadow-sm" : "text-zinc-500 hover:text-zinc-700"
                        }`}>
                        {m === "list" ? "列表" : "查找"}
                      </button>
                    ))}
                  </div>

                  {queryMode === "list" ? (
                    <div className="flex gap-4">
                      <div>
                        <div className="flex items-baseline gap-1 mb-1.5">
                          <span className="text-[11px] font-medium text-zinc-600 font-mono">size</span>
                          <span className="text-[10px] text-zinc-400">页面大小 (-1 = 全部)</span>
                        </div>
                        <input type="number" min={-1}
                          value={fields.listSize}
                          onChange={(e) => { setField("listSize", e.target.value); setField("listPage", "1"); }}
                          placeholder="-1"
                          className="w-20 px-3 py-1.5 rounded-md border border-zinc-200 text-[12px] font-mono bg-white
                            focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400 placeholder-zinc-300" />
                      </div>
                      <div>
                        <div className="flex items-baseline gap-1 mb-1.5">
                          <span className="text-[11px] font-medium text-zinc-600 font-mono">page</span>
                          <span className="text-[10px] text-zinc-400">页码 (≥1)</span>
                        </div>
                        <input type="number" min={1}
                          value={fields.listPage}
                          onChange={(e) => setField("listPage", e.target.value)}
                          placeholder="1"
                          className="w-20 px-3 py-1.5 rounded-md border border-zinc-200 text-[12px] font-mono bg-white
                            focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400 placeholder-zinc-300" />
                      </div>
                    </div>
                  ) : (
                    <>
                      {/* service_id input */}
                      <div className="shrink-0">
                        <div className="flex items-baseline gap-1 mb-1">
                          <span className="text-[11px] font-medium text-zinc-600 font-mono">service_id</span>
                          <span className="text-[10px] text-red-400">必填</span>
                          <span className="text-[10px] text-zinc-400">(skill 类型将下载 ZIP)</span>
                        </div>
                        <div className="flex items-center gap-1.5">
                          <input type="text"
                            value={fields.serviceId}
                            onChange={(e) => setField("serviceId", e.target.value)}
                            placeholder="输入服务 ID，或浏览下方列表选择"
                            className="flex-1 px-3 py-1.5 rounded-md border border-zinc-200 text-[12px] font-mono bg-white
                              focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400 placeholder-zinc-300" />
                          {fields.serviceId && (
                            <button onClick={() => setField("serviceId", "")}
                              className="shrink-0 text-zinc-300 hover:text-zinc-600 transition-colors p-1">
                              <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                              </svg>
                            </button>
                          )}
                        </div>
                      </div>
                      {/* Service browser — fills remaining space */}
                      <RegistryBrowser
                        dataset={activeDataset}
                        selected={fields.serviceId}
                        onSelect={(id) => setField("serviceId", id)}
                        refreshKey={browserRefreshKey}
                      />
                    </>
                  )}
                </div>
              )}

              {/* ── Build ── */}
              {op === "build" && (
                <div className="space-y-5">
                  {/* Disclaimer + log toggle */}
                  <div className="rounded-md bg-amber-50 border border-amber-200 px-3 py-2 text-[11px] text-amber-700 leading-relaxed">
                    构建分类树需要大量 LLM 调用，大型数据集耗时较长。发起请求后可关闭页面，后台继续运行。
                  </div>
                  <label className="flex items-center gap-2 text-[12px] text-zinc-500 cursor-pointer select-none -mt-2">
                    <input type="checkbox" checked={showBuildLogs} onChange={(e) => setShowBuildLogs(e.target.checked)}
                      className="accent-zinc-700 w-3.5 h-3.5" />
                    实时显示构建日志
                  </label>
                  {/* Build mode */}
                  <div>
                    <div className="text-[11px] font-medium text-zinc-500 mb-2">构建模式</div>
                    <div className="flex rounded-lg border border-zinc-200 overflow-hidden shadow-sm">
                      {BUILD_MODES.map((r, i) => (
                        <button key={r.val} onClick={() => setResume(r.val)}
                          className={`flex-1 px-3 py-2 text-left transition-colors ${i > 0 ? "border-l border-zinc-200" : ""} ${
                            resume === r.val ? "bg-zinc-800" : "bg-white hover:bg-zinc-50"
                          }`}>
                          <div className={`text-[12px] font-medium ${resume === r.val ? "text-white" : "text-zinc-700"}`}>
                            {r.label}
                          </div>
                          <div className={`text-[11px] mt-0.5 leading-snug ${resume === r.val ? "text-zinc-300" : "text-zinc-500"}`}>
                            {r.desc}
                          </div>
                        </button>
                      ))}
                    </div>
                  </div>

                  {/* Embedding model */}
                  <div>
                    <div className="text-[11px] font-medium text-zinc-500 mb-2">Embedding 模型（向量检索）</div>
                    <select
                      value={embeddingModel}
                      onChange={(e) => {
                        setEmbeddingModel(e.target.value);
                        // Save immediately
                        fetch(`/api/datasets/${encodeURIComponent(activeDataset)}/vector-config`, {
                          method: "POST",
                          headers: { "Content-Type": "application/json" },
                          body: JSON.stringify({ embedding_model: e.target.value }),
                        }).then((r) => r.json()).then(() => setDatasetEmbeddingModel(e.target.value)).catch(console.error);
                      }}
                      className="w-full rounded-md border border-zinc-200 bg-white px-3 py-2 text-[12px] text-zinc-700 focus:ring-1 focus:ring-zinc-400 focus:border-zinc-400"
                    >
                      {Object.entries(embeddingModels).map(([name, info]) => (
                        <option key={name} value={name}>
                          {name} — {info.description} (dim={info.dim})
                        </option>
                      ))}
                    </select>
                    {embeddingModel !== datasetEmbeddingModel && (
                      <div className="mt-1 text-[11px] text-amber-600">
                        ⚠ 模型已变更，向量索引将在后台自动重建
                      </div>
                    )}
                  </div>

                  {/* Core params */}
                  <ParamGroup title="树结构">
                    <div className="grid grid-cols-2 gap-2">
                      <NumField label="generic_ratio" placeholder="0.333" note="通用分类阈值"
                        value={fields.genericRatio} onChange={(v) => setField("genericRatio", v)} />
                      <NumField label="delete_threshold" placeholder="2" note="删除阈值"
                        value={fields.deleteThreshold} onChange={(v) => setField("deleteThreshold", v)} />
                      <NumField label="max_service_size" placeholder="40" note="节点最大服务数"
                        value={fields.maxServiceSize} onChange={(v) => setField("maxServiceSize", v)} />
                      <NumField label="max_categories_size" placeholder="20" note="最大子类数"
                        value={fields.maxCategoriesSize} onChange={(v) => setField("maxCategoriesSize", v)} />
                      <NumField label="max_depth" placeholder="3" note="最大深度"
                        value={fields.maxDepth} onChange={(v) => setField("maxDepth", v)} />
                      <NumField label="min_leaf_size" placeholder="5" note="最小叶节点服务数"
                        value={fields.minLeafSize} onChange={(v) => setField("minLeafSize", v)} />
                    </div>
                  </ParamGroup>

                  <ParamGroup title="关键词提取">
                    <div className="grid grid-cols-2 gap-2">
                      <NumField label="keyword_batch_size" placeholder="50" note="批次大小"
                        value={fields.keywordBatchSize} onChange={(v) => setField("keywordBatchSize", v)} />
                      <NumField label="max_keywords_per_service" placeholder="5" note="每服务最大关键词"
                        value={fields.maxKeywordsPerSvc} onChange={(v) => setField("maxKeywordsPerSvc", v)} />
                      <NumField label="keyword_threshold" placeholder="500" note="关键词 vs 描述阈值"
                        value={fields.keywordThreshold} onChange={(v) => setField("keywordThreshold", v)} />
                    </div>
                  </ParamGroup>

                  <ParamGroup title="分类迭代">
                    <div className="grid grid-cols-2 gap-2">
                      <NumField label="classification_retries" placeholder="2" note="分类重试次数"
                        value={fields.classRetries} onChange={(v) => setField("classRetries", v)} />
                      <NumField label="max_refine_iterations" placeholder="3" note="精细化迭代次数"
                        value={fields.maxRefineIter} onChange={(v) => setField("maxRefineIter", v)} />
                    </div>
                  </ParamGroup>

                  <ParamGroup title="LLM 温度">
                    <div className="grid grid-cols-3 gap-2">
                      <NumField label="temperature_keywords" placeholder="0.0"
                        value={fields.tempKeywords} onChange={(v) => setField("tempKeywords", v)} />
                      <NumField label="temperature_design" placeholder="0.0"
                        value={fields.tempDesign} onChange={(v) => setField("tempDesign", v)} />
                      <NumField label="temperature_classify" placeholder="0.0"
                        value={fields.tempClassify} onChange={(v) => setField("tempClassify", v)} />
                    </div>
                  </ParamGroup>

                  <ParamGroup title="LLM Token 限制">
                    <div className="grid grid-cols-2 gap-2">
                      <NumField label="max_tokens_design" placeholder="6000"
                        value={fields.maxTokDesign} onChange={(v) => setField("maxTokDesign", v)} />
                      <NumField label="max_tokens_design_small" placeholder="4000"
                        value={fields.maxTokDesignSmall} onChange={(v) => setField("maxTokDesignSmall", v)} />
                      <NumField label="max_tokens_classify" placeholder="300"
                        value={fields.maxTokClassify} onChange={(v) => setField("maxTokClassify", v)} />
                      <NumField label="max_tokens_validate" placeholder="3000"
                        value={fields.maxTokValidate} onChange={(v) => setField("maxTokValidate", v)} />
                      <NumField label="max_tokens_keywords" placeholder="4000"
                        value={fields.maxTokKeywords} onChange={(v) => setField("maxTokKeywords", v)} />
                    </div>
                  </ParamGroup>

                  <ParamGroup title="并发 & 跨域">
                    <div className="space-y-2">
                      <div className="grid grid-cols-2 gap-2">
                        <NumField label="workers" placeholder="20" note="并发数"
                          value={fields.buildWorkers} onChange={(v) => setField("buildWorkers", v)} />
                      </div>
                    </div>
                  </ParamGroup>

                </div>
              )}
            </div>
          </section>
        </div>

        {/* Submit / Cancel */}
        <div className="shrink-0 px-6 py-4 border-t border-zinc-100 bg-[#fafafa]">
          {op === "build" && isPolling ? (
            <button onClick={handleCancelBuild}
              className="w-full py-2.5 rounded-lg text-white text-[13px] font-semibold tracking-wide
                bg-zinc-500 hover:bg-zinc-600 active:bg-zinc-700 transition-colors">
              中止构建
            </button>
          ) : (
            <button onClick={handleSubmit} disabled={!canSubmit}
              className="w-full py-2.5 rounded-lg text-white text-[13px] font-semibold tracking-wide
                bg-[#C7000B] hover:bg-[#a8000a] active:bg-[#8a0008]
                transition-colors disabled:opacity-30 disabled:cursor-not-allowed">
              {loading ? (
                <span className="inline-flex items-center gap-2">
                  <svg className="animate-spin h-4 w-4" viewBox="0 0 24 24">
                    <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
                    <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
                  </svg>
                  处理中
                </span>
              ) : op === "register" ? "注册服务" : op === "deregister" ? "确认注销" : op === "query" ? "执行查询" : "启动构建"}
            </button>
          )}
        </div>
      </aside>

      {/* ── RIGHT: request preview + draggable divider + response ── */}
      <div ref={panelRef} className="flex-1 min-w-0 flex flex-col overflow-hidden bg-[#fafafa] px-6 py-5">

        <>
            {/* Request preview */}
            <div style={{flex: splitPct, minHeight: 0}} className="flex flex-col pb-1">
              <div className="text-[11px] font-semibold text-zinc-400 uppercase tracking-wider mb-2">请求预览</div>
              <div className="flex-1 min-h-0 rounded-lg overflow-hidden border border-zinc-700/40 shadow-sm bg-zinc-900 flex flex-col">
                {preview.map((step, i) => (
                  <div key={i} className={`shrink-0 flex items-center gap-3 px-4 py-2 ${i > 0 ? "border-t" : ""} border-b border-zinc-700/60 bg-zinc-800`}>
                    <span className={`text-[10px] font-bold font-mono ${
                      step.method === "DELETE" ? "text-red-400"
                      : step.method === "GET" ? "text-sky-400"
                      : "text-emerald-400"
                    }`}>{step.method}</span>
                    <span className="text-[11px] text-zinc-200 font-mono flex-1 min-w-0 truncate">{step.path}</span>
                    {step.body && (
                      <span className="text-[10px] text-zinc-500 font-mono shrink-0">json</span>
                    )}
                  </div>
                ))}
                <pre className="flex-1 min-h-0 px-4 py-3 text-[11px] font-mono text-zinc-300 leading-relaxed overflow-auto whitespace-pre">
                  {bodyStr}
                </pre>
              </div>
            </div>

            {/* Drag handle */}
            <div onMouseDown={handleDragStart}
              className="shrink-0 h-3 cursor-ns-resize flex items-center justify-center group my-0.5">
              <div className="w-10 h-[3px] rounded-full bg-zinc-200 group-hover:bg-zinc-400 transition-colors" />
            </div>

            {/* Response */}
            <div style={{flex: 100 - splitPct, minHeight: 0}} className="flex flex-col pt-1">
              <div className="flex items-center gap-2 mb-2">
                <span className="text-[11px] font-semibold text-zinc-400 uppercase tracking-wider">响应</span>
                {isPolling && (
                  <span className="flex items-center gap-1 text-[10px] text-blue-500">
                    <svg className="animate-spin h-3 w-3" viewBox="0 0 24 24">
                      <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
                      <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
                    </svg>
                    轮询构建状态...
                  </span>
                )}
              </div>
              <div className="flex-1 min-h-0 rounded-lg border border-zinc-200 bg-white overflow-hidden flex flex-col shadow-sm">
                {!response && !loading && !reqError && (
                  <div className="flex-1 flex items-center justify-center">
                    <p className="text-[13px] text-zinc-300">发起请求后，响应将显示在这里</p>
                  </div>
                )}
                {loading && !response && (
                  <div className="flex-1 flex items-center justify-center">
                    <svg className="animate-spin h-7 w-7 text-zinc-200" viewBox="0 0 24 24">
                      <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
                      <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
                    </svg>
                  </div>
                )}
                {reqError && <div className="p-4 text-[12px] text-red-600 font-mono">{reqError}</div>}
                {response && (
                  <>
                    <div className={`shrink-0 flex items-center gap-3 px-4 py-2.5 border-b border-zinc-100 ${
                      response.status >= 200 && response.status < 300 ? "bg-emerald-50" : "bg-red-50"
                    }`}>
                      <span className={`text-[12px] font-bold font-mono ${
                        response.status >= 200 && response.status < 300 ? "text-emerald-700" : "text-red-700"
                      }`}>{response.status}</span>
                      <span className="text-[11px] text-zinc-400">{response.elapsed.toFixed(2)}s</span>
                      {respStatus && (
                        <span className={`text-[10px] px-2 py-0.5 rounded-full font-medium ${
                          respStatus === "done" || respStatus === "registered" || respStatus === "deregistered"
                            ? "bg-emerald-100 text-emerald-700"
                            : respStatus === "error"
                              ? "bg-red-100 text-red-700"
                              : respStatus === "running" || respStatus === "started"
                                ? "bg-blue-100 text-blue-700"
                                : "bg-zinc-100 text-zinc-600"
                        }`}>{respStatus}</span>
                      )}
                    </div>
                    {isPolling && showBuildLogs ? (
                      <div className="flex-1 min-h-0 overflow-y-auto bg-zinc-950 p-3">
                        {((response.body as any)?.logs as string[] | undefined)?.length
                          ? ((response.body as any).logs as string[]).map((line, i) => (
                              <div key={i} className="font-mono text-[11px] text-zinc-300 leading-relaxed py-px">{line}</div>
                            ))
                          : <div className="font-mono text-[11px] text-zinc-500 py-px">等待日志输出…</div>
                        }
                        <div ref={logEndRef} />
                      </div>
                    ) : (
                      <div className="flex-1 min-h-0 overflow-y-auto">
                        <pre className="px-4 py-3 text-[12px] font-mono text-zinc-700 leading-relaxed whitespace-pre-wrap">
                          {responseBodyStr}
                        </pre>
                      </div>
                    )}
                    {/* Query list pagination: next page button (read from headers) */}
                    {op === "query" && queryMode === "list" && (() => {
                      const meta = response.pagination;
                      const hasNext = meta && meta.page < meta.total_pages;
                      return hasNext ? (
                        <div className="shrink-0 px-4 py-2.5 border-t border-zinc-100 bg-zinc-50 flex items-center justify-between">
                          <span className="text-[11px] text-zinc-400">
                            第 {meta!.page} / {meta!.total_pages} 页
                          </span>
                          <button
                            onClick={async () => {
                              const nextPage = String(meta!.page + 1);
                              setField("listPage", nextPage);
                              setLoading(true);
                              setResponse(null);
                              setReqError(null);
                              startRef.current = Date.now();
                              try {
                                const params = new URLSearchParams({
                                  fields: "detail",
                                  size: fields.listSize || "-1",
                                  page: nextPage,
                                });
                                const resp = await fetch(
                                  `/api/datasets/${encodeURIComponent(activeDataset)}/services?${params}`,
                                  { method: "GET" },
                                );
                                const body = await resp.json();
                                const total = resp.headers.get("X-Total-Count");
                                const pg = resp.headers.get("X-Page");
                                const tp = resp.headers.get("X-Total-Pages");
                                const ps = resp.headers.get("X-Page-Size");
                                const pagination = total && pg && tp && ps
                                  ? { page: +pg, total_pages: +tp, total_count: +total, page_size: +ps }
                                  : undefined;
                                setResponse({ status: resp.status, body, elapsed: (Date.now() - startRef.current) / 1000, pagination });
                              } catch (e) {
                                setReqError(String(e));
                              } finally {
                                setLoading(false);
                              }
                            }}
                            className="shrink-0 px-3 py-1 rounded-md bg-zinc-800 text-white text-[11px] font-medium
                              hover:bg-zinc-700 transition-colors">
                            下一页 →
                          </button>
                        </div>
                      ) : (
                        <div className="shrink-0 px-4 py-2 border-t border-zinc-100 bg-zinc-50">
                          <span className="text-[11px] text-zinc-400">
                            {meta ? `共 ${meta.total_pages} 页` : "已到最后一页"}
                          </span>
                        </div>
                      );
                    })()}
                  </>
                )}
              </div>
            </div>
        </>
      </div>
    </div>
  );
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function SLabel({ n, text }: { n: number; text: string }) {
  return (
    <div className="flex items-center gap-2 mb-0.5">
      <span className="w-5 h-5 rounded bg-[#C7000B] text-white text-[11px] font-bold flex items-center justify-center leading-none shrink-0">
        {n}
      </span>
      <span className="text-[15px] font-semibold text-zinc-800">{text}</span>
    </div>
  );
}

function ParamGroup({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div>
      <div className="text-[10px] font-semibold text-zinc-400 uppercase tracking-widest pb-1 mb-2 border-b border-zinc-100">
        {title}
        <span className="text-zinc-300 font-normal normal-case tracking-normal ml-1">（留空使用默认值）</span>
      </div>
      {children}
    </div>
  );
}

function PersistentToggle({ value, onChange }: { value: boolean; onChange: (v: boolean) => void }) {
  return (
    <label className="flex items-center gap-2 text-[12px] text-zinc-500 cursor-pointer select-none">
      <input type="checkbox" checked={value} onChange={(e) => onChange(e.target.checked)}
        className="accent-zinc-700 w-3.5 h-3.5" />
      persistent — 写入 api_config.json 持久保存
    </label>
  );
}

/**
 * RegistryBrowser — full-panel overlay that lists services registered via the registry API.
 * Calls /api/datasets/{dataset}/services?fields=detail. Clicking a service calls onSelect and closes via onClose.
 */
function RegistryBrowser({
  dataset,
  selected,
  onSelect,
  refreshKey,
}: {
  dataset: string;
  selected: string;
  onSelect: (id: string, entry?: ServiceEntry) => void;
  refreshKey: number;
}) {
  const [services, setServices] = useState<ServiceEntry[]>([]);
  const [searchQuery, setSearchQuery] = useState("");
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!dataset) return;
    setLoading(true);
    setServices([]);
    fetch(`/api/datasets/${encodeURIComponent(dataset)}/services?fields=detail`)
      .then((r) => r.json())
      .then((data: ServiceEntry[]) => { setServices(Array.isArray(data) ? data : []); setLoading(false); })
      .catch(() => setLoading(false));
  }, [dataset, refreshKey]);

  const q = searchQuery.toLowerCase();
  const filtered = useMemo(() => {
    const list = q
      ? services.filter((s) =>
          s.id.toLowerCase().includes(q) ||
          s.name.toLowerCase().includes(q) ||
          s.description.toLowerCase().includes(q)
        )
      : services;
    // editable first, locked (user_config) last
    return [...list].sort((a, b) => {
      const aLocked = a.source === "user_config" ? 1 : 0;
      const bLocked = b.source === "user_config" ? 1 : 0;
      return aLocked - bLocked;
    });
  }, [services, q]);

  return (
    <div className="flex-1 min-h-0 rounded-lg border border-zinc-200/80 bg-white overflow-hidden flex flex-col">
      {/* Header */}
      <div className="shrink-0 px-4 py-2.5 bg-zinc-50 border-b border-zinc-100 flex items-center justify-between">
        <div>
          <span className="text-[13px] font-semibold text-zinc-700">{dataset}</span>
          <span className="text-[11px] text-zinc-400 ml-2">
            {loading
              ? "加载中"
              : `${services.length} 个已注册服务${filtered.length !== services.length ? ` · 筛选 ${filtered.length}` : ""}`
            }
          </span>
        </div>
      </div>

      {/* Search */}
      <div className="shrink-0 px-4 py-2 border-b border-zinc-100">
        <div className="relative">
          <svg className="absolute left-2.5 top-1/2 -translate-y-1/2 w-3 h-3 text-zinc-400 pointer-events-none"
            fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
              d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
          </svg>
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder={`搜索 ID / 名称 / 描述… (共 ${services.length} 项)`}
            className="w-full pl-7 pr-3 py-1.5 rounded-md border border-zinc-200 text-[12px] bg-white
              focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400 placeholder-zinc-300"
          />
        </div>
      </div>

      {/* List */}
      <div className="flex-1 overflow-y-auto divide-y divide-zinc-50">
        {loading ? (
          <div className="flex items-center justify-center py-10 text-[12px] text-zinc-400">加载中…</div>
        ) : filtered.length === 0 ? (
          <div className="flex items-center justify-center py-10 text-[12px] text-zinc-400">
            {services.length === 0 ? "该数据集暂无已注册服务" : "无匹配服务"}
          </div>
        ) : (
          filtered.map((s) => {
            const locked = s.source === "user_config";
            const isSkill = s.source === "skill_folder";
            const isSel = !locked && selected === s.id;
            return (
              <button
                key={s.id}
                onClick={() => !locked && onSelect(s.id, s)}
                disabled={locked}
                className={`w-full text-left px-4 py-2.5 transition-colors ${
                  locked
                    ? "opacity-40 cursor-not-allowed"
                    : isSel
                      ? "bg-zinc-800"
                      : "hover:bg-zinc-50"
                }`}
              >
                <div className="flex items-baseline gap-2">
                  <span className={`text-[11px] font-mono font-medium truncate flex-1 min-w-0 ${isSel ? "text-white" : "text-zinc-700"}`}>
                    {s.id}
                  </span>
                  {locked && (
                    <span className="shrink-0 text-[9px] px-1.5 py-0.5 rounded border border-zinc-200 text-zinc-400 bg-zinc-50 font-medium">
                      user_config
                    </span>
                  )}
                  {isSkill && (
                    <span className="shrink-0 text-[9px] px-1.5 py-0.5 rounded border border-blue-200 text-blue-500 bg-blue-50 font-medium">
                      skill
                    </span>
                  )}
                </div>
                {s.name && (
                  <div className={`text-[12px] font-medium mt-0.5 truncate ${isSel ? "text-white/90" : "text-zinc-600"}`}>{s.name}</div>
                )}
                {s.description && (
                  <div className={`text-[11px] mt-0.5 line-clamp-2 leading-snug ${isSel ? "text-white/60" : "text-zinc-400"}`}>{s.description}</div>
                )}
              </button>
            );
          })
        )}
      </div>

      {/* Footer: selected ID + confirm hint */}
      <div className="shrink-0 px-4 py-2.5 border-t border-zinc-100 bg-zinc-50 flex items-center gap-2 min-h-[40px]">
        {selected ? (
          <>
            <span className="text-[11px] text-zinc-400 shrink-0">已选中：</span>
            <span className="text-[11px] font-mono font-medium text-zinc-700 flex-1 min-w-0 truncate">{selected}</span>
            <button onClick={() => onSelect("")} className="text-zinc-300 hover:text-zinc-600 transition-colors p-0.5 shrink-0">
              <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
              </svg>
            </button>
          </>
        ) : (
          <span className="text-[11px] text-zinc-300">点击上方服务以选中，将自动返回</span>
        )}
      </div>
    </div>
  );
}

function Field({
  label, note, required, placeholder, defaultValue, value, onChange,
  textarea, rows, expand, mono,
}: {
  label: string;
  note?: string;
  required?: boolean;
  placeholder?: string;
  defaultValue?: string;
  value: string;
  onChange: (v: string) => void;
  textarea?: boolean;
  rows?: number;
  expand?: boolean;
  mono?: boolean;
}) {
  const hasDefault = !!defaultValue && !value;
  const displayPlaceholder = defaultValue ?? placeholder ?? "";

  const base = `w-full px-3 py-1.5 rounded-md border text-[12px] bg-white
    focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400
    ${mono ? "font-mono" : ""}
    ${hasDefault ? "border-zinc-200 placeholder-zinc-400/80" : "border-zinc-200 placeholder-zinc-300"}`;

  return (
    <div className={expand ? "flex flex-col flex-1 min-h-0" : ""}>
      <div className="flex items-baseline gap-1 mb-1 shrink-0">
        <span className="text-[11px] font-medium text-zinc-600 font-mono">{label}</span>
        {required && <span className="text-[10px] text-red-400 font-normal">必填</span>}
        {note && <span className="text-[10px] text-zinc-400 font-normal">({note})</span>}
        {hasDefault && (
          <span className="text-[10px] text-zinc-400/70 font-normal ml-auto">使用默认值</span>
        )}
      </div>
      {textarea ? (
        <textarea
          rows={expand ? undefined : (rows ?? 3)}
          value={value}
          onChange={(e) => onChange(e.target.value)}
          placeholder={displayPlaceholder}
          className={`${base} ${expand ? "flex-1 min-h-0 resize-none" : "resize-none"}`}
        />
      ) : (
        <input type="text" value={value}
          onChange={(e) => onChange(e.target.value)}
          placeholder={displayPlaceholder}
          className={base} />
      )}
    </div>
  );
}

function NumField({ label, note, placeholder, value, onChange }: {
  label: string; note?: string; placeholder?: string;
  value: string; onChange: (v: string) => void;
}) {
  return (
    <div>
      <div className="text-[10px] font-medium text-zinc-500 font-mono mb-1">
        {label}
        {note && <span className="text-zinc-400 font-normal ml-1">({note})</span>}
      </div>
      <input type="number" value={value} onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        className="w-full px-2.5 py-1.5 rounded-md border border-zinc-200 text-[12px] bg-white font-mono
          focus:outline-none focus:ring-1 focus:ring-zinc-400/40 focus:border-zinc-400 placeholder-zinc-300" />
    </div>
  );
}
