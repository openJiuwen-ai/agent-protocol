import { useEffect, useState, useMemo, useCallback } from "react";
import type { ServiceResult } from "../types";

interface EmbeddingModelInfo {
  dim: number;
  language: string;
  description: string;
}

interface Props {
  dataset: string;
  onClose: () => void;
  refreshKey?: number;
}

export default function ServiceBrowser({ dataset, onClose, refreshKey = 0 }: Props) {
  const [services, setServices] = useState<ServiceResult[]>([]);
  const [loading, setLoading] = useState(true);
  const [search, setSearch] = useState("");
  const [embeddingModel, setEmbeddingModel] = useState("");
  const [embeddingModels, setEmbeddingModels] = useState<Record<string, EmbeddingModelInfo>>({});
  const [syncing, setSyncing] = useState(false);

  useEffect(() => {
    fetch("/api/datasets/embedding-models")
      .then((r) => r.json())
      .then((data) => setEmbeddingModels(data.models || {}))
      .catch(console.error);
  }, []);

  useEffect(() => {
    setLoading(true);
    Promise.all([
      fetch(`/api/datasets/${encodeURIComponent(dataset)}/services?fields=brief`).then((r) => r.json()),
      fetch(`/api/datasets/${encodeURIComponent(dataset)}/vector-config`).then((r) => r.json()),
    ])
      .then(([svcData, vcData]) => {
        setServices(svcData as ServiceResult[]);
        setEmbeddingModel(vcData.embedding_model || "");
        setLoading(false);
      })
      .catch(() => setLoading(false));
  }, [dataset, refreshKey]);

  const handleModelChange = useCallback(
    (newModel: string) => {
      if (newModel === embeddingModel || syncing) return;
      setSyncing(true);
      setEmbeddingModel(newModel);
      fetch(`/api/datasets/${encodeURIComponent(dataset)}/vector-config`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ embedding_model: newModel }),
      })
        .then((r) => r.json())
        .then(() => {
          // Wait a bit for background sync to start, then clear
          setTimeout(() => setSyncing(false), 2000);
        })
        .catch(() => setSyncing(false));
    },
    [dataset, embeddingModel, syncing],
  );

  const filtered = useMemo(() => {
    if (!search.trim()) return services;
    const q = search.toLowerCase();
    return services.filter(
      (s) => s.name.toLowerCase().includes(q) || s.description.toLowerCase().includes(q),
    );
  }, [services, search]);

  const modelInfo = embeddingModels[embeddingModel];

  return (
    <div className="rounded-lg border border-zinc-200/80 bg-white overflow-hidden flex flex-col h-full">
      {/* Header */}
      <div className="px-4 py-2.5 bg-zinc-50 border-b border-zinc-100 flex items-center justify-between shrink-0">
        <div>
          <span className="text-[13px] font-semibold text-zinc-700">{dataset}</span>
          <span className="text-[11px] text-zinc-400 ml-2">
            {loading
              ? "加载中"
              : `${services.length} 个服务${filtered.length !== services.length ? ` · 筛选 ${filtered.length}` : ""}`}
          </span>
        </div>
        <button
          onClick={onClose}
          className="text-zinc-400 hover:text-zinc-600 transition-colors"
          title="关闭"
        >
          <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" />
          </svg>
        </button>
      </div>

      {/* Embedding model bar */}
      <div className="px-4 py-2 border-b border-zinc-100 shrink-0 flex items-center gap-2">
        <span className="text-[11px] text-zinc-400 whitespace-nowrap">Embedding</span>
        <select
          value={embeddingModel}
          onChange={(e) => handleModelChange(e.target.value)}
          disabled={syncing}
          className="flex-1 rounded border border-zinc-200 bg-white px-2 py-1 text-[11px] text-zinc-600
            focus:ring-1 focus:ring-zinc-400 focus:border-zinc-400 disabled:opacity-50"
        >
          {Object.entries(embeddingModels).map(([name, info]) => (
            <option key={name} value={name}>
              {name} — {info.description}
            </option>
          ))}
        </select>
        {syncing && (
          <div className="flex items-center gap-1.5">
            <div className="w-3 h-3 border-2 border-zinc-300 border-t-zinc-600 rounded-full animate-spin" />
            <span className="text-[10px] text-zinc-400">同步中</span>
          </div>
        )}
        {!syncing && modelInfo && (
          <span className="text-[10px] text-zinc-400 whitespace-nowrap tabular-nums">
            dim={modelInfo.dim}
          </span>
        )}
      </div>

      {/* Search */}
      <div className="px-4 py-2 border-b border-zinc-100 shrink-0">
        <input
          type="text"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          placeholder="搜索服务名或描述…"
          className="w-full px-3 py-1.5 rounded-md border border-zinc-200 text-[12px]
            placeholder-zinc-400 focus:outline-none focus:ring-1 focus:ring-[#C7000B]/30"
        />
      </div>

      {/* Service list */}
      <div className="flex-1 overflow-y-auto divide-y divide-zinc-50">
        {loading ? (
          <p className="text-center py-10 text-[12px] text-zinc-400">加载中…</p>
        ) : filtered.length === 0 ? (
          <p className="text-center py-10 text-[12px] text-zinc-400">无匹配</p>
        ) : (
          filtered.map((svc, idx) => (
            <div key={svc.id} className="px-4 py-2 hover:bg-zinc-50/50 transition-colors">
              <div className="flex items-baseline gap-2">
                <span className="text-[10px] text-zinc-300 w-6 text-right tabular-nums">{idx + 1}</span>
                <span className="text-[12px] font-medium text-zinc-700">{svc.name}</span>
                <span className="text-[10px] text-zinc-300 font-mono">{svc.id}</span>
              </div>
              <p className="text-[11px] text-zinc-400 leading-relaxed ml-8 mt-0.5 line-clamp-2">
                {svc.description}
              </p>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
