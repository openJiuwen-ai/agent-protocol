import { useState, useCallback, useEffect, useRef } from "react";
import DatasetSelector from "./components/DatasetSelector";
import QueryInput from "./components/QueryInput";
import MethodSelector from "./components/MethodSelector";
import SearchResultCard from "./components/SearchResultCard";
import ComparisonChart from "./components/ComparisonChart";
import ServiceBrowser from "./components/ServiceBrowser";
import AdminPanel from "./components/AdminPanel";
import { useSearch } from "./hooks/useSearch";
import type { SearchMethod, TaxonomyNode, NavigationStep, SearchResponse } from "./types";

interface MethodState {
  loading: boolean;
  liveSteps: NavigationStep[];
  result: SearchResponse | null;
  error: string | null;
}

function parseMethodKey(key: string): { method: SearchMethod; topK: number } {
  if (key.startsWith("vector_")) return { method: "vector", topK: parseInt(key.split("_")[1], 10) };
  return { method: key as SearchMethod, topK: 10 };
}

const DISPLAY_ORDER = [
  "vector_1", "vector_5", "vector_10",
  "a2x_get_one", "a2x_get_important", "a2x_get_all",
  "traditional",
];

export default function App() {
  const [mode, setMode] = useState<"search" | "admin">("search");
  const [browserVersion, setBrowserVersion] = useState(0);

  const switchMode = useCallback((m: "search" | "admin") => {
    if (m === "search" && mode === "admin") setBrowserVersion((v) => v + 1);
    setMode(m);
  }, [mode]);

  // Warmup polling state
  const [warmupReady, setWarmupReady] = useState(false);
  const [warmupStage, setWarmupStage] = useState("连接服务器...");
  const [warmupProgress, setWarmupProgress] = useState(0);

  // Search state
  const [dataset, setDataset] = useState("ToolRet_clean");
  const [query, setQuery] = useState("");
  const [methods, setMethods] = useState<string[]>(["a2x_get_important", "vector_5"]);
  const [taxonomy, setTaxonomy] = useState<TaxonomyNode | null>(null);
  const [placeholderQuery, setPlaceholderQuery] = useState("");
  const [searchedQuery, setSearchedQuery] = useState("");
  const [searchedTaxonomy, setSearchedTaxonomy] = useState<TaxonomyNode | null>(null);
  const [queryGlow, setQueryGlow] = useState<"idle" | "searching" | "fading">("idle");
  const [showBrowser, setShowBrowser] = useState(false);
  const [states, setStates] = useState<Record<string, MethodState>>({});
  const liveStepsRefs = useRef<Record<string, NavigationStep[]>>({});

  // Provider selector state
  const [providers, setProviders] = useState<{ name: string; model: string }[]>([]);
  const [currentProvider, setCurrentProvider] = useState("");

  const hasAnyLoading = Object.values(states).some((s) => s.loading);

  // Transition glow from searching → fading when all results arrive
  useEffect(() => {
    if (!hasAnyLoading && queryGlow === "searching") setQueryGlow("fading");
  }, [hasAnyLoading, queryGlow]);

  // ── Warmup polling ───────────────────────────────────────────────────────
  useEffect(() => {
    let cancelled = false;
    const poll = async () => {
      while (!cancelled) {
        try {
          const resp = await fetch("/api/warmup-status");
          const data = await resp.json();
          if (!cancelled) {
            setWarmupStage(data.stage || "加载中...");
            setWarmupProgress(data.progress ?? 0);
            if (data.ready) { setWarmupReady(true); return; }
          }
        } catch {
          if (!cancelled) setWarmupStage("连接服务器...");
        }
        await new Promise((r) => setTimeout(r, 500));
      }
    };
    poll();
    return () => { cancelled = true; };
  }, []);

  // ── Load providers (after warmup) ────────────────────────────────────────
  useEffect(() => {
    if (!warmupReady) return;
    fetch("/api/providers")
      .then((r) => r.json())
      .then((data) => {
        setProviders(data.providers || []);
        setCurrentProvider(data.current || "");
      })
      .catch(() => {});
  }, [warmupReady]);

  const switchProvider = (name: string) => {
    fetch(`/api/providers/${name}`, { method: "POST" })
      .then((r) => r.json())
      .then((data) => { if (data.current) setCurrentProvider(data.current); })
      .catch(() => {});
  };

  // ── Load taxonomy when dataset changes ───────────────────────────────────
  useEffect(() => {
    setTaxonomy(null);
    fetch(`/api/datasets/${dataset}/taxonomy`)
      .then((r) => { if (!r.ok) throw new Error(`${r.status}`); return r.json(); })
      .then((t) => setTaxonomy(t))
      .catch(() => {
        setTaxonomy(null);
        // Drop A2X methods if there's no taxonomy; fall back to vector_5 if nothing remains
        setMethods((prev) => {
          const next = prev.filter((m) => !m.startsWith("a2x"));
          return next.length > 0 ? next : ["vector_5"];
        });
      });
  }, [dataset]);

  // ── Search ───────────────────────────────────────────────────────────────
  const onStep = useCallback((tag: string, step: NavigationStep) => {
    const arr = liveStepsRefs.current[tag] || [];
    arr.push(step);
    liveStepsRefs.current[tag] = arr;
    setStates((prev) => ({ ...prev, [tag]: { ...prev[tag], liveSteps: [...arr] } }));
  }, []);

  const onResult = useCallback((tag: string, result: SearchResponse) => {
    setStates((prev) => ({ ...prev, [tag]: { ...prev[tag], loading: false, result } }));
  }, []);

  const onError = useCallback((tag: string, error: string) => {
    setStates((prev) => ({ ...prev, [tag]: { ...prev[tag], loading: false, error } }));
  }, []);

  const { searchOne } = useSearch({ onStep, onResult, onError });

  const handleSearch = () => {
    if (methods.length === 0) return;
    const effectiveQuery = query.trim() || placeholderQuery;
    if (!effectiveQuery) return;

    if (!query.trim() && placeholderQuery) setQuery(placeholderQuery);
    setSearchedQuery(effectiveQuery);
    setSearchedTaxonomy(taxonomy);
    setQueryGlow("searching");
    setShowBrowser(false);
    liveStepsRefs.current = {};

    const init: Record<string, MethodState> = {};
    for (const key of methods) {
      init[key] = { loading: true, liveSteps: [], result: null, error: null };
      liveStepsRefs.current[key] = [];
    }
    setStates(init);

    for (const key of methods) {
      const { method, topK } = parseMethodKey(key);
      searchOne(effectiveQuery, method, dataset, topK, key);
    }
  };

  const activeMethods = DISPLAY_ORDER.filter((m) => m in states);
  const completedResults: Record<string, SearchResponse> = {};
  for (const m of activeMethods) {
    if (states[m]?.result) completedResults[m] = states[m].result!;
  }
  const allCompleted = activeMethods.length > 0 && activeMethods.every((m) => !states[m]?.loading);

  // ── Loading screen ───────────────────────────────────────────────────────
  if (!warmupReady) {
    return (
      <div className="h-screen flex flex-col items-center justify-center bg-[#fafafa]">
        <div className="flex flex-col items-center gap-8 w-80">
          <div className="flex items-center gap-3">
            <div className="w-14 h-14 rounded-lg bg-[#C7000B] flex items-center justify-center shadow-lg">
              <span className="text-white font-extrabold text-[20px] tracking-tighter">A2X</span>
            </div>
            <span className="text-[32px] font-bold text-zinc-800 tracking-tight">A2X Registry</span>
          </div>
          <div className="w-full flex flex-col gap-3">
            <div className="w-full h-1.5 bg-zinc-200 rounded-full overflow-hidden">
              <div
                className="h-full bg-[#C7000B] rounded-full transition-all duration-500 ease-out"
                style={{ width: `${warmupProgress}%` }}
              />
            </div>
            <div className="flex items-center justify-between">
              <p className="text-[13px] text-zinc-600 font-medium">{warmupStage}</p>
              <p className="text-[12px] text-zinc-400 tabular-nums">{warmupProgress}%</p>
            </div>
          </div>
        </div>
      </div>
    );
  }

  // ── Main UI ──────────────────────────────────────────────────────────────
  return (
    <div className="h-screen flex flex-col bg-[#fafafa]">
      {/* Header */}
      <header className="shrink-0 z-50 bg-white border-b border-zinc-200/80 shadow-sm">
        <div className="flex items-center justify-between px-6 py-3">
          {/* Mode toggle */}
          <div className="w-48 flex items-center">
            <div className="flex rounded-lg border border-zinc-200 overflow-hidden text-[11px] font-medium shadow-sm">
              <button
                onClick={() => switchMode("admin")}
                className={`px-3 py-1.5 transition-colors ${
                  mode === "admin" ? "bg-[#C7000B] text-white" : "bg-white text-zinc-500 hover:bg-zinc-50"
                }`}
              >管理员</button>
              <button
                onClick={() => switchMode("search")}
                className={`px-3 py-1.5 transition-colors border-l border-zinc-200 ${
                  mode === "search" ? "bg-zinc-800 text-white border-zinc-800" : "bg-white text-zinc-500 hover:bg-zinc-50"
                }`}
              >用户</button>
            </div>
          </div>

          {/* Logo */}
          <div className="flex items-center gap-2.5">
            <div className="w-10 h-10 rounded-md bg-[#C7000B] flex items-center justify-center">
              <span className="text-white font-extrabold text-[14px] tracking-tighter">A2X</span>
            </div>
            <span className="text-[26px] font-bold text-zinc-800 tracking-tight">A2X Registry</span>
          </div>

          {/* Provider selector */}
          <div className="w-48 flex justify-end">
            {providers.length > 0 && (
              <div className="flex items-center gap-1.5">
                <svg className="w-3.5 h-3.5 text-zinc-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2}
                    d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" />
                </svg>
                <select
                  value={currentProvider}
                  onChange={(e) => switchProvider(e.target.value)}
                  className="text-[12px] text-zinc-600 bg-zinc-50 border border-zinc-200 rounded-md px-2 py-1
                    focus:outline-none focus:ring-1 focus:ring-[#C7000B]/30 cursor-pointer hover:border-zinc-300"
                >
                  {providers.map((p) => (
                    <option key={p.name} value={p.name}>{p.name} ({p.model})</option>
                  ))}
                </select>
              </div>
            )}
          </div>
        </div>
      </header>

      {/* Body */}
      <main className="flex-1 flex overflow-hidden">

        {/* ── Admin mode panel ── */}
        {mode === "admin" && (
          <div key="admin" className="animate-fade-in flex-1 min-w-0 flex">
            <AdminPanel />
          </div>
        )}

        {/* ── User mode panel ── */}
        {mode === "search" && <>

        {/* Left sidebar — search controls */}
        <aside className="animate-fade-in w-2/5 shrink-0 bg-white border-r border-zinc-200/80 flex flex-col">
          <div className="flex-1 flex flex-col px-6 py-6 min-h-0 gap-9">

            {/* 1. Dataset */}
            <section className="shrink-0">
              <SectionLabel num={1} text="选择数据集" />
              <div className="mt-3">
                <DatasetSelector
                  value={dataset}
                  onChange={(v) => { setDataset(v); setShowBrowser(false); }}
                  refreshKey={browserVersion}
                />
              </div>
              <button
                onClick={() => setShowBrowser((p) => !p)}
                className={`mt-2 w-full px-3 py-2 rounded-md text-[13px] transition-all
                  border border-dashed flex items-center justify-center gap-1.5 font-medium
                  ${showBrowser
                    ? "border-[#C7000B]/40 bg-[#C7000B]/5 text-[#C7000B]"
                    : "border-zinc-300 text-zinc-600 hover:border-zinc-400 hover:text-zinc-800 hover:bg-zinc-50"
                  }`}
              >
                <svg className="w-4 h-4 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                  <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M4 6h16M4 10h16M4 14h16M4 18h16" />
                </svg>
                浏览数据集
              </button>
            </section>

            {/* 2. Query — grows to fill remaining height */}
            <section className="flex-1 flex flex-col min-h-0">
              <SectionLabel num={2} text="输入请求" />
              <div className="mt-3 flex-1 flex flex-col min-h-0">
                <QueryInput
                  value={query}
                  onChange={setQuery}
                  dataset={dataset}
                  disabled={hasAnyLoading}
                  onPlaceholderChange={setPlaceholderQuery}
                  glowState={queryGlow}
                  onGlowEnd={() => setQueryGlow("idle")}
                />
              </div>
            </section>

            {/* 3. Method selector */}
            <section className="shrink-0">
              <SectionLabel num={3} text="选择搜索方式" />
              <div className="mt-3">
                <MethodSelector
                  value={methods}
                  onChange={setMethods}
                  disabled={hasAnyLoading}
                  a2xAvailable={taxonomy !== null}
                />
              </div>
            </section>

          </div>

          <div className="px-6 py-4 border-t border-zinc-100 bg-zinc-50/50">
            <button
              onClick={handleSearch}
              disabled={hasAnyLoading || methods.length === 0}
              className="w-full py-3 rounded-lg text-white text-[14px] font-semibold tracking-wide
                bg-[#C7000B] hover:bg-[#a8000a] active:bg-[#8a0008]
                transition-colors disabled:opacity-30 disabled:cursor-not-allowed shadow-sm"
            >
              {hasAnyLoading ? (
                <span className="inline-flex items-center gap-2">
                  <svg className="animate-spin h-4 w-4" viewBox="0 0 24 24">
                    <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
                    <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
                  </svg>
                  搜索中
                </span>
              ) : "搜索相关服务"}
            </button>
          </div>
        </aside>

        {/* Right panel — results */}
        <div className="flex-1 min-w-0 overflow-y-auto px-8 py-6 space-y-5 animate-fade-in">
          {showBrowser ? (
            <ServiceBrowser dataset={dataset} onClose={() => setShowBrowser(false)} refreshKey={browserVersion} />
          ) : (
            <>
              {activeMethods.length === 0 && (
                <div className="h-full flex flex-col items-center justify-center gap-4">
                  <div className="w-16 h-16 rounded-2xl bg-zinc-100 flex items-center justify-center">
                    <svg className="w-8 h-8 text-zinc-300" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.2}
                        d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
                    </svg>
                  </div>
                  <div className="text-center">
                    <p className="text-[17px] font-semibold text-zinc-500 mb-1.5">搜索结果</p>
                    <p className="text-[13px] text-zinc-400 leading-relaxed">
                      在左侧选择数据集、输入请求并选择搜索方式<br />然后点击「搜索相关服务」查看对比结果
                    </p>
                  </div>
                </div>
              )}

              {activeMethods.map((m) => {
                const st = states[m];
                if (!st) return null;
                return (
                  <SearchResultCard
                    key={m}
                    methodKey={m}
                    state={st}
                    query={searchedQuery}
                    taxonomy={searchedTaxonomy}
                    parseMethodKey={parseMethodKey}
                  />
                );
              })}

              {activeMethods.length > 0 && (
                <ComparisonChart results={completedResults} query={searchedQuery} loading={!allCompleted} />
              )}

              {activeMethods.length > 0 && (
                <div className="flex justify-center pt-2 pb-4">
                  <button
                    onClick={() => { setStates({}); liveStepsRefs.current = {}; setSearchedQuery(""); }}
                    className="inline-flex items-center gap-1 px-3 py-1.5 rounded text-[11px] text-zinc-400 hover:text-zinc-600 hover:bg-zinc-100 transition-colors"
                  >
                    <svg className="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
                    </svg>
                    清除结果
                  </button>
                </div>
              )}
            </>
          )}
        </div>
        </>}{/* end user mode panel */}
      </main>
    </div>
  );
}

function SectionLabel({ num, text }: { num: number; text: string }) {
  return (
    <div className="flex items-center gap-2 mb-0.5">
      <span className="w-5 h-5 rounded bg-[#C7000B] text-white text-[11px] font-bold flex items-center justify-center leading-none shrink-0">
        {num}
      </span>
      <span className="text-[15px] font-semibold text-zinc-800">{text}</span>
    </div>
  );
}
