import type { SearchResponse, SearchMethod } from "../types";

interface Props {
  result: SearchResponse;
  method: SearchMethod;
  query: string;
}

export default function ResultPanel({ result, method, query }: Props) {
  const isA2X = method.startsWith("a2x");
  const isTraditional = method === "traditional";
  const isVector = !isA2X && !isTraditional;

  return (
    <div className="space-y-3 animate-fade-in-up">
      {/* Stat pills */}
      <div className="flex items-center gap-1.5 flex-wrap">
        <Pill label="结果" value={result.results.length} accent />
        {(isA2X || isTraditional) && (
          <>
            <Pill label="LLM" value={result.stats.llm_calls} />
            <Pill label="Token" value={result.stats.total_tokens.toLocaleString()} />
          </>
        )}
        {isA2X && (
          <>
            {result.stats.visited_categories != null && (
              <Pill label="访问" value={result.stats.visited_categories} />
            )}
            {result.stats.pruned_categories != null && (
              <Pill label="剪枝" value={result.stats.pruned_categories} />
            )}
          </>
        )}
        {isVector && <Pill label="方式" value="嵌入向量" />}
        <span className="ml-auto text-[11px] text-zinc-300 truncate max-w-[220px]">{query}</span>
      </div>

      {/* Services */}
      {result.results.length === 0 ? (
        <p className="text-center py-6 text-[12px] text-zinc-400">未找到相关服务</p>
      ) : (
        <div className="max-h-[130px] overflow-y-auto">
          {result.results.map((svc, idx) => (
            <div key={svc.id} className="flex items-baseline gap-1.5 py-0.5 animate-fade-in-up" style={{ animationDelay: `${idx * 15}ms` }}>
              <span className="text-[11px] text-zinc-300 w-3 text-right shrink-0 tabular-nums">{idx + 1}</span>
              <span className="text-[13px] font-medium text-zinc-700 shrink-0">{svc.name}</span>
              <span className="text-[12px] text-zinc-400 truncate flex-1">{svc.description}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

function Pill({ label, value, accent }: { label: string; value: string | number; accent?: boolean }) {
  return (
    <span className={`inline-flex items-center gap-0.5 px-1.5 py-[2px] rounded text-[10px] ${
      accent ? "bg-[#C7000B]/8 text-[#C7000B] font-semibold" : "bg-zinc-100 text-zinc-500"
    }`}>
      {label} <strong className="font-semibold">{value}</strong>
    </span>
  );
}
