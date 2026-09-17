/**
 * SearchResultCard — renders one method's search result in the right panel.
 *
 * Shows a colored header with method label + live status, an optional
 * D3.js taxonomy tree animation for A2X methods, and the final ResultPanel.
 */

import ResultPanel from "./ResultPanel";
import TreeAnimation from "./TreeAnimation";
import { getMethodColor } from "../methodColors";
import type { NavigationStep, SearchResponse, SearchTrace, TaxonomyNode } from "../types";

interface MethodState {
  loading: boolean;
  liveSteps: NavigationStep[];
  result: SearchResponse | null;
  error: string | null;
}

interface SearchResultCardProps {
  methodKey: string;     // e.g. "a2x_get_important" or "vector_5"
  state: MethodState;
  query: string;
  taxonomy: TaxonomyNode | null;
  parseMethodKey: (key: string) => { method: string; topK: number };
}

export default function SearchResultCard({
  methodKey,
  state,
  query,
  taxonomy,
  parseMethodKey,
}: SearchResultCardProps) {
  const isA2X = methodKey.startsWith("a2x");
  const c = getMethodColor(methodKey);
  const liveTrace: SearchTrace = {
    navigation_steps: state.liveSteps.filter((s) => !s.parent_id.startsWith("__")),
  };

  const statusText = isA2X
    ? state.liveSteps.some((s) => s.parent_id === "__fallback__")
      ? "Fallback → Get Important"
      : state.liveSteps.some((s) => s.parent_id === "__phase2__")
        ? "筛选服务中..."
        : `导航中 (${state.liveSteps.filter((s) => s.parent_id !== "__phase2__").length})`
    : "检索中";

  return (
    <div className={`rounded-lg border border-zinc-200/80 bg-white overflow-hidden border-l-[3px] ${c.accent}`}>
      {/* Header */}
      <div className={`px-4 py-2 ${c.bg} flex items-center justify-between gap-3`}>
        <span className={`text-[12px] font-semibold tracking-wide ${c.text} shrink-0`}>{c.label}</span>

        {state.loading && (
          <span className={`inline-flex items-center gap-1.5 text-[11px] ${c.text} opacity-80`}>
            <svg className="animate-spin h-3 w-3 shrink-0" viewBox="0 0 24 24">
              <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
              <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
            <span className="truncate">{statusText}</span>
          </span>
        )}

        {state.result && (
          <span className="text-[11px] text-zinc-400 shrink-0 ml-auto tabular-nums">
            {state.result.results.length} 个结果 · {state.result.elapsed_time}s
          </span>
        )}

        {state.error && (
          <span className="text-[11px] text-red-500 truncate ml-auto">{state.error}</span>
        )}
      </div>

      {/* Body */}
      <div className="px-4 py-3 space-y-3">
        {isA2X && (state.loading || state.liveSteps.length > 0) && taxonomy && (
          <TreeAnimation
            trace={liveTrace}
            taxonomyTree={taxonomy}
            selectedServiceIds={state.result ? new Set(state.result.results.map((r) => r.id)) : null}
          />
        )}

        {!isA2X && state.loading && (
          <div className="flex items-center justify-center py-8">
            <svg className="animate-spin h-6 w-6 text-zinc-300" viewBox="0 0 24 24">
              <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" fill="none" />
              <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
            </svg>
          </div>
        )}

        {state.result && (
          <ResultPanel
            result={state.result}
            method={parseMethodKey(methodKey).method as import("../types").SearchMethod}
            query={query}
          />
        )}
      </div>
    </div>
  );
}
