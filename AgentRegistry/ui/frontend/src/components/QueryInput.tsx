import { useState, useEffect, useCallback, useRef } from "react";
import type { DefaultQuery } from "../types";

interface Props {
  value: string;
  onChange: (v: string) => void;
  dataset: string;
  disabled?: boolean;
  onPlaceholderChange?: (v: string) => void;
  glowState?: "idle" | "searching" | "fading";
  onGlowEnd?: () => void;
}

export default function QueryInput({ value, onChange, dataset, disabled, onPlaceholderChange, glowState = "idle", onGlowEnd }: Props) {
  const [defaults, setDefaults] = useState<DefaultQuery[]>([]);
  const [idx, setIdx] = useState(0);
  const [lang, setLang] = useState<"cn" | "en">("cn");
  const prevSourceRef = useRef("");
  const onPlaceholderRef = useRef(onPlaceholderChange);
  onPlaceholderRef.current = onPlaceholderChange;

  const getPlaceholder = useCallback((data: DefaultQuery[], i: number, l: "cn" | "en") => {
    if (data.length === 0) return "输入你的请求...";
    const q = data[i % data.length];
    return (l === "en" && q.query_en) ? q.query_en : q.query;
  }, []);

  // Load defaults when dataset changes; preserve selection if query source is identical
  useEffect(() => {
    fetch(`/api/datasets/${encodeURIComponent(dataset)}/default-queries`)
      .then((r) => r.json())
      .then((resp: { source: string; queries: DefaultQuery[] }) => {
        const { source, queries } = resp;
        if (source && source === prevSourceRef.current) {
          // Same query source (e.g. CN ↔ EN variant via $ref) — keep current selection
          return;
        }
        prevSourceRef.current = source;
        setDefaults(queries);
        setIdx(0);
        setLang("cn");
        onPlaceholderRef.current?.(getPlaceholder(queries, 0, "cn"));
      })
      .catch(console.error);
  }, [dataset, getPlaceholder]);

  const currentPlaceholder = getPlaceholder(defaults, idx, lang);

  // Sync parent whenever idx/lang/defaults change
  useEffect(() => {
    onPlaceholderRef.current?.(currentPlaceholder);
  }, [currentPlaceholder]);

  const cycleNext = () => {
    if (defaults.length <= 1) return;
    const next = (idx + 1) % defaults.length;
    setIdx(next);
    onChange(getPlaceholder(defaults, next, lang));
  };

  const toggleLang = () => {
    const current = defaults[idx];
    if (!current?.query_en) return;
    const newLang = lang === "cn" ? "en" : "cn";
    setLang(newLang);
    onChange(getPlaceholder(defaults, idx, newLang));
  };

  const hasEn = defaults[idx]?.query_en;

  return (
    <div
      onAnimationEnd={glowState === "fading" ? onGlowEnd : undefined}
      className={`flex-1 min-h-0 flex flex-col rounded-lg border border-zinc-200 bg-white overflow-hidden
        transition-shadow
        ${glowState === "searching" ? "query-searching" : ""}
        ${glowState === "fading" ? "query-fading" : ""}
        ${glowState === "idle" ? "focus-within:ring-1 focus-within:ring-[#C7000B]/30 focus-within:border-[#C7000B]/40" : ""}`}
    >
      <textarea
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={currentPlaceholder}
        disabled={disabled}
        className="flex-1 min-h-0 w-full px-4 pt-3 pb-2 bg-transparent
          text-[14px] text-zinc-800 placeholder-zinc-400 focus:outline-none
          disabled:cursor-not-allowed resize-none leading-relaxed"
      />

      {/* Controls bar — inside the box */}
      <div className="shrink-0 flex items-center justify-end gap-1.5 px-3 py-2 border-t border-zinc-100">
        <button
          onClick={toggleLang}
          disabled={!hasEn}
          title={lang === "cn" ? "切换英文" : "切换中文"}
          className={`px-2 py-0.5 rounded text-[10px] font-medium border transition-all
            ${lang === "en"
              ? "bg-zinc-700 text-white border-zinc-700"
              : "text-zinc-500 border-zinc-300 hover:border-zinc-400 hover:text-zinc-700"}
            disabled:opacity-30 disabled:cursor-not-allowed`}
        >
          {lang === "cn" ? "中" : "EN"}
        </button>
        <button
          onClick={cycleNext}
          disabled={defaults.length <= 1}
          title="换一条示例"
          className="px-2 py-0.5 rounded text-[10px] font-medium border border-zinc-300
            text-zinc-500 hover:text-zinc-700 hover:border-zinc-400 transition-all
            disabled:opacity-30 disabled:cursor-not-allowed"
        >
          换一条
        </button>
      </div>
    </div>
  );
}
