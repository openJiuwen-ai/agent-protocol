import { useState, useEffect } from "react";
import type { SearchResponse } from "../types";
import { getMethodColor, METHOD_COLORS } from "../methodColors";

interface Props {
  results: Record<string, SearchResponse>;
  query: string;
  loading?: boolean;
}

interface JudgeResult {
  service_id: string;
  relevant: boolean;
}

export default function ComparisonChart({ results, query, loading = false }: Props) {
  const methodKeys = Object.keys(results);
  const [judgeMap, setJudgeMap] = useState<Record<string, boolean>>({});
  const [judging, setJudging] = useState(false);

  // Build service map
  const serviceMap = new Map<string, { name: string; description: string; methods: string[] }>();
  for (const [mk, res] of Object.entries(results)) {
    for (const svc of res.results) {
      const existing = serviceMap.get(svc.id);
      if (existing) existing.methods.push(mk);
      else serviceMap.set(svc.id, { name: svc.name, description: svc.description, methods: [mk] });
    }
  }

  // Sort: relevant first (if judged), then by method count, then alphabetically
  const services = Array.from(serviceMap.entries()).sort(([idA, a], [idB, b]) => {
    const relA = judgeMap[idA];
    const relB = judgeMap[idB];
    if (relA !== undefined && relB !== undefined) {
      if (relA !== relB) return relA ? -1 : 1;
    }
    return a.name.localeCompare(b.name);
  });

  const totalUnique = services.length;
  const foundByAll = services.filter(([, s]) => s.methods.length === methodKeys.length).length;
  // Per-method: count relevant/irrelevant, sorted by relevant count desc
  const stats = methodKeys.map((k) => {
    const svcIds = results[k].results.map((s) => s.id);
    const rel = svcIds.filter((id) => judgeMap[id] === true).length;
    const irr = svcIds.filter((id) => judgeMap[id] === false).length;
    return { key: k, c: getMethodColor(k), count: svcIds.length, rel, irr };
  }).sort((a, b) => {
    const ratioA = a.count > 0 ? a.rel / a.count : 0;
    const ratioB = b.count > 0 ? b.rel / b.count : 0;
    return ratioB - ratioA || b.rel - a.rel;
  });

  // Auto-judge only after all methods complete
  useEffect(() => {
    if (loading || totalUnique === 0 || !query) return;
    setJudging(true);
    setJudgeMap({});

    const allServices = services.map(([id, svc]) => ({
      id, name: svc.name, description: svc.description,
    }));

    fetch("/api/search/judge", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ query, services: allServices }),
    })
      .then((r) => r.json())
      .then((data: { results: JudgeResult[] }) => {
        const map: Record<string, boolean> = {};
        for (const jr of data.results) map[jr.service_id] = jr.relevant;
        setJudgeMap(map);
      })
      .catch(() => {})
      .finally(() => setJudging(false));
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [loading, totalUnique, query]);

  const relevantCount = Object.values(judgeMap).filter(Boolean).length;
  const irrelevantCount = Object.values(judgeMap).filter((v) => v === false).length;

  return (
    <div className="rounded-lg border border-zinc-200/80 bg-white overflow-hidden animate-fade-in-up">
      <div className="px-4 py-2 bg-zinc-50 border-b border-zinc-100 flex items-center justify-between">
        <span className="text-[13px] font-semibold text-zinc-700">总览</span>
        <div className="flex items-center gap-3">
          {Object.keys(judgeMap).length > 0 && (
            <span className="text-[11px] text-zinc-400">
              <span className="text-emerald-600 font-medium">{relevantCount} 相关</span>
              {" · "}
              <span className="text-red-500 font-medium">{irrelevantCount} 无关</span>
            </span>
          )}
          {judging && (
            <span className="inline-flex items-center gap-1 text-[11px] text-zinc-400">
              <svg className="animate-spin h-3 w-3" viewBox="0 0 24 24"><circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" /><path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" /></svg>
              评判中
            </span>
          )}
          <span className="text-[11px] text-zinc-400">
            合计 <strong className="text-zinc-600">{totalUnique}</strong> · 共同 <strong className="text-zinc-600">{foundByAll}</strong>
          </span>
        </div>
      </div>

      <div className="px-4 py-3 space-y-3 relative">
        {loading ? (
          <div className="flex items-center justify-center py-8">
            <svg className="animate-spin h-6 w-6 text-zinc-300" viewBox="0 0 24 24"><circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" /><path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" /></svg>
          </div>
        ) : (<>
        {/* Method badges */}
        <div className="flex items-center gap-1 flex-wrap">
          {stats.map((s, i) => (
            <span key={s.key} className="inline-flex items-center">
              {i > 0 && <span className="text-zinc-400 text-[13px] mx-1 font-bold">▸</span>}
              <span className={`inline-flex items-center gap-1.5 px-2 py-0.5 rounded ${s.c.chartBg}`}>
                <span className={`w-2 h-2 rounded-full ${s.c.pill}`} />
                <span className={`text-[11px] font-medium ${s.c.chartText}`}>{s.c.label}</span>
                {Object.keys(judgeMap).length > 0 ? (
                  <span className={`text-[10px] font-semibold ${s.c.chartText}`}>{s.rel}/{s.count}</span>
                ) : (
                  <span className={`text-[10px] ${s.c.chartText} opacity-50`}>{s.count}</span>
                )}
              </span>
            </span>
          ))}
        </div>

        {/* Judging overlay */}
        {judging && (
          <div className="absolute inset-0 z-10 flex flex-col items-center justify-center bg-white/80 backdrop-blur-sm rounded-lg">
            <svg className="animate-spin h-8 w-8 text-indigo-500 mb-2" viewBox="0 0 24 24">
              <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
              <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            <span className="text-sm text-zinc-500 font-medium">LLM 评判相关性...</span>
          </div>
        )}

        {/* Service list */}
        <div className="max-h-[280px] overflow-y-auto -mx-4 px-4">
          {services.map(([id, svc]) => {
            const rel = judgeMap[id];
            const hasJudge = rel !== undefined;
            return (
              <div
                key={id}
                className={`flex items-baseline gap-2 py-1 rounded transition-colors ${
                  hasJudge && !rel ? "opacity-35" : "hover:bg-zinc-50"
                }`}
              >
                {/* Relevance badge */}
                <span className="w-[30px] shrink-0 text-center">
                  {hasJudge ? (
                    rel ? (
                      <span className="text-[9px] font-semibold text-emerald-600 bg-emerald-50 px-1 py-px rounded-full">相关</span>
                    ) : (
                      <span className="text-[9px] font-semibold text-zinc-400 bg-zinc-100 px-1 py-px rounded-full">无关</span>
                    )
                  ) : judging ? (
                    <span className="text-[9px] text-zinc-300">···</span>
                  ) : null}
                </span>
                {/* Method dots */}
                <div className="flex gap-0.5 shrink-0 items-center">
                  {methodKeys.map((mk) => (
                    <span
                      key={mk}
                      className={`w-2 h-2 rounded-full transition-opacity ${svc.methods.includes(mk) ? getMethodColor(mk).pill : "bg-zinc-150 opacity-20"}`}
                      title={svc.methods.includes(mk) ? (METHOD_COLORS[mk]?.label ?? mk) : "—"}
                    />
                  ))}
                </div>
                <span className="text-[12px] font-medium text-zinc-700 shrink-0">{svc.name}</span>
                <span className="text-[11px] text-zinc-400 truncate flex-1">{svc.description}</span>
              </div>
            );
          })}
        </div>
        </>)}
      </div>
    </div>
  );
}
