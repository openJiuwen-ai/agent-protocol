import { getMethodColor } from "../methodColors";

interface Props {
  value: string[];
  onChange: (v: string[]) => void;
  disabled?: boolean;
  a2xAvailable?: boolean;
}

const VECTOR_ITEMS = [
  { key: "vector_1", label: "Top-1" },
  { key: "vector_5", label: "Top-5" },
  { key: "vector_10", label: "Top-10" },
];

const A2X_ITEMS = [
  { key: "a2x_get_one", label: "Get One", note: "最相关的服务" },
  { key: "a2x_get_important", label: "Get Important", note: "同类服务去重" },
  { key: "a2x_get_all", label: "Get All", note: "所有相关服务" },
];

const TRADITIONAL_ITEMS = [
  { key: "traditional_full", label: "Full Context", disabled: true, note: "单次查询token>200k" },
  { key: "traditional", label: "Description Only", note: "等待时间较久" },
];

export default function MethodSelector({ value, onChange, disabled, a2xAvailable = true }: Props) {
  const toggle = (key: string) => {
    if (value.includes(key)) {
      const next = value.filter((m) => m !== key);
      if (next.length > 0) onChange(next);
    } else {
      onChange([...value, key]);
    }
  };

  const a2xItems = a2xAvailable
    ? A2X_ITEMS
    : A2X_ITEMS.map((i) => ({ ...i, disabled: true, note: "无分类树" }));

  return (
    <div className="grid grid-cols-3 gap-5">
      <Column title="文本向量 + ANN" items={VECTOR_ITEMS} value={value} toggle={toggle} disabled={disabled} />
      <Column title="A2X 层级分类" items={a2xItems} value={value} toggle={toggle} disabled={disabled} />
      <Column title="MCP 经典方案" items={TRADITIONAL_ITEMS} value={value} toggle={toggle} disabled={disabled} />
    </div>
  );
}

function Column({ title, items, value, toggle, disabled }: {
  title: string;
  items: { key: string; label: string; disabled?: boolean; note?: string }[];
  value: string[];
  toggle: (key: string) => void;
  disabled?: boolean;
}) {
  const headerColor = getMethodColor(items.find((i) => !i.disabled)?.key ?? items[0].key);
  return (
    <div>
      <div className={`text-[11px] font-semibold text-zinc-500 uppercase tracking-widest pb-1.5 mb-2 border-b ${headerColor.border}`}>
        {title}
      </div>
      <div className="space-y-0.5">
        {items.map((item) => {
          const itemDisabled = item.disabled;
          const checked = !itemDisabled && value.includes(item.key);
          const c = getMethodColor(item.key);
          return (
            <button
              key={item.key}
              onClick={() => !itemDisabled && toggle(item.key)}
              disabled={disabled || itemDisabled}
              className={`w-full flex items-center gap-2 px-2.5 py-1.5 rounded-md text-[13px] transition-all ${
                itemDisabled
                  ? "text-zinc-400 cursor-not-allowed"
                  : checked
                    ? `${c.bg} ${c.text} font-medium`
                    : "text-zinc-700 hover:bg-zinc-50 hover:text-zinc-900"
              }`}
            >
              <span className={`w-3.5 h-3.5 rounded-full border-[1.5px] flex items-center justify-center shrink-0 transition-all ${
                itemDisabled
                  ? "border-zinc-300 bg-transparent"
                  : checked
                    ? `${c.pill} shadow-sm`
                    : "border-zinc-300 bg-white"
              }`}>
                {checked && (
                  <svg className="w-2 h-2 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={3} d="M5 13l4 4L19 7" />
                  </svg>
                )}
              </span>
              <span className="flex-1 text-left">{item.label}</span>
              {item.note && <span className="text-[9px] text-zinc-400 shrink-0">{item.note}</span>}
            </button>
          );
        })}
      </div>
    </div>
  );
}
