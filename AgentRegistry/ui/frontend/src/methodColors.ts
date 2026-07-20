/**
 * Two-accent color system:
 *   A2X  → Red family (#C7000B) — warm, authoritative
 *   Vector → Amber/neutral — secondary, complementary
 * Everything else → zinc neutrals
 */

export interface MethodColor {
  label: string;
  bg: string;
  text: string;
  border: string;
  pill: string;
  pillBorder: string;
  chartBg: string;
  chartText: string;
  accent: string;       // for border-l accent stripe
}

export const METHOD_COLORS: Record<string, MethodColor> = {
  vector_1: {
    label: "向量 Top-1",
    bg: "bg-yellow-50/60",
    text: "text-yellow-800",
    border: "border-yellow-300",
    pill: "bg-yellow-500 border-yellow-500",
    pillBorder: "shadow-yellow-100",
    chartBg: "bg-yellow-50",
    chartText: "text-yellow-800",
    accent: "border-l-yellow-400",
  },
  vector_5: {
    label: "向量 Top-5",
    bg: "bg-amber-50/60",
    text: "text-amber-800",
    border: "border-amber-300",
    pill: "bg-amber-500 border-amber-500",
    pillBorder: "shadow-amber-100",
    chartBg: "bg-amber-50",
    chartText: "text-amber-800",
    accent: "border-l-amber-400",
  },
  vector_10: {
    label: "向量 Top-10",
    bg: "bg-orange-50/60",
    text: "text-orange-800",
    border: "border-orange-300",
    pill: "bg-orange-500 border-orange-500",
    pillBorder: "shadow-orange-100",
    chartBg: "bg-orange-50",
    chartText: "text-orange-800",
    accent: "border-l-orange-400",
  },
  a2x_get_one: {
    label: "A2X Get One",
    bg: "bg-indigo-50/60",
    text: "text-indigo-700",
    border: "border-indigo-300",
    pill: "bg-indigo-600 border-indigo-600",
    pillBorder: "shadow-indigo-100",
    chartBg: "bg-indigo-50",
    chartText: "text-indigo-700",
    accent: "border-l-indigo-500",
  },
  a2x_get_all: {
    label: "A2X Get All",
    bg: "bg-violet-50/60",
    text: "text-violet-700",
    border: "border-violet-300",
    pill: "bg-violet-500 border-violet-500",
    pillBorder: "shadow-violet-100",
    chartBg: "bg-violet-50",
    chartText: "text-violet-700",
    accent: "border-l-violet-400",
  },
  a2x_get_important: {
    label: "A2X Get Important",
    bg: "bg-purple-50/60",
    text: "text-purple-700",
    border: "border-purple-300",
    pill: "bg-purple-500 border-purple-500",
    pillBorder: "shadow-purple-100",
    chartBg: "bg-purple-50",
    chartText: "text-purple-700",
    accent: "border-l-purple-400",
  },
  traditional: {
    label: "MCP 经典方案",
    bg: "bg-slate-50/60",
    text: "text-slate-700",
    border: "border-slate-300",
    pill: "bg-slate-500 border-slate-500",
    pillBorder: "shadow-slate-100",
    chartBg: "bg-slate-50",
    chartText: "text-slate-700",
    accent: "border-l-slate-400",
  },
};

export function getMethodColor(key: string): MethodColor {
  return METHOD_COLORS[key] ?? {
    label: key,
    bg: "bg-zinc-50",
    text: "text-zinc-600",
    border: "border-zinc-200",
    pill: "bg-zinc-400 border-zinc-400",
    pillBorder: "shadow-zinc-100",
    chartBg: "bg-zinc-50",
    chartText: "text-zinc-600",
    accent: "border-l-zinc-300",
  };
}
