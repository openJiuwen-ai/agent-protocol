import { useEffect, useState } from "react";
import type { DatasetInfo } from "../types";

interface Props {
  value: string;
  onChange: (v: string) => void;
  refreshKey?: number;
}

export default function DatasetSelector({ value, onChange, refreshKey }: Props) {
  const [datasets, setDatasets] = useState<DatasetInfo[]>([]);

  useEffect(() => {
    fetch("/api/datasets").then((r) => r.json()).then((list: DatasetInfo[]) => {
      setDatasets(list);
      // Auto-select first dataset if current value is not in the list
      if (list.length > 0 && !list.some((d) => d.name === value)) {
        onChange(list[0].name);
      }
    }).catch(console.error);
  }, [refreshKey]);

  const compact = datasets.length > 3;

  return (
    <div className="flex flex-wrap items-stretch gap-2">
      {datasets.map((d) => {
        const active = value === d.name;
        return compact ? (
          /* Compact pill for 4+ datasets */
          <button
            key={d.name}
            onClick={() => onChange(d.name)}
            className={`relative rounded-md px-3 py-1.5 text-[13px] font-medium transition-all border ${
              active
                ? "border-[#C7000B] bg-[#C7000B]/5 text-[#C7000B] shadow-sm"
                : "border-zinc-200 bg-white text-zinc-600 hover:border-zinc-300 hover:shadow-sm"
            }`}
          >
            {d.name}
            <span className={`ml-1.5 text-[11px] ${active ? "text-[#C7000B]/60" : "text-zinc-400"}`}>
              {d.service_count}
            </span>
          </button>
        ) : (
          /* Card style for ≤3 datasets */
          <button
            key={d.name}
            onClick={() => onChange(d.name)}
            className={`relative flex-1 rounded-lg px-4 py-2.5 text-left transition-all border-2 ${
              active
                ? "border-[#C7000B] bg-[#C7000B]/5 shadow-sm"
                : "border-zinc-200 bg-white hover:border-zinc-300 hover:shadow-sm"
            }`}
          >
            {active && (
              <span className="absolute top-2 right-2 w-2 h-2 rounded-full bg-[#C7000B]" />
            )}
            <div className={`text-[14px] font-semibold ${active ? "text-[#C7000B]" : "text-zinc-700"}`}>
              {d.name}
            </div>
            <div className="mt-0.5">
              <span className={`text-[11px] ${active ? "text-[#C7000B]/70" : "text-zinc-500"}`}>
                {d.service_count} 服务
              </span>
            </div>
          </button>
        );
      })}
    </div>
  );
}
