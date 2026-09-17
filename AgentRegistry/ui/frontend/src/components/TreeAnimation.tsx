import { useEffect, useRef, useMemo } from "react";
import * as d3 from "d3";
import type { SearchTrace, TaxonomyNode } from "../types";

interface Props {
  trace: SearchTrace;
  taxonomyTree: TaxonomyNode | null;
  selectedServiceIds: Set<string> | null; // null = Phase 2 not done yet
}

interface TreeNode {
  id: string;
  name: string;
  isService?: boolean;
  children?: TreeNode[];
}

type NodeState = "selected" | "pruned" | "pending" | "svc-selected" | "svc-rejected" | "svc-pending" | "default";

export default function TreeAnimation({ trace, taxonomyTree, selectedServiceIds }: Props) {
  const svgRef = useRef<SVGSVGElement>(null);
  const gRef = useRef<SVGGElement | null>(null);
  const prevNodeCountRef = useRef(0);
  const zoomRef = useRef<d3.ZoomBehavior<SVGSVGElement, unknown> | null>(null);
  const initializedRef = useRef(false);

  // Memoize taxonomy lookup
  const taxMap = useMemo(() => {
    if (!taxonomyTree) return new Map<string, TaxonomyNode>();
    const map = new Map<string, TaxonomyNode>();
    const walk = (n: TaxonomyNode) => { map.set(n.id, n); n.children.forEach(walk); };
    walk(taxonomyTree);
    return map;
  }, [taxonomyTree]);

  // Build tree structure from trace + taxonomy + service results
  const treeData = useMemo(() => {
    if (!taxonomyTree || taxMap.size === 0) {
      return { root: null, nodeStates: new Map<string, NodeState>() };
    }

    const selectedCatIds = new Set<string>();
    const prunedCatIds = new Set<string>();
    selectedCatIds.add(taxonomyTree.id);

    for (const step of trace.navigation_steps) {
      selectedCatIds.add(step.parent_id);
      step.selected.forEach((id) => selectedCatIds.add(id));
      step.pruned.forEach((id) => prunedCatIds.add(id));
    }

    const visibleIds = new Set<string>();
    const pendingCatIds = new Set<string>();
    const nodeStates = new Map<string, NodeState>();

    // Categories
    for (const id of selectedCatIds) {
      visibleIds.add(id);
      nodeStates.set(id, "selected");
      const taxNode = taxMap.get(id);
      if (taxNode) {
        for (const child of taxNode.children) {
          visibleIds.add(child.id);
          if (!selectedCatIds.has(child.id) && !prunedCatIds.has(child.id)) {
            pendingCatIds.add(child.id);
            nodeStates.set(child.id, "pending");
          }
        }
      }
    }
    for (const id of prunedCatIds) {
      visibleIds.add(id);
      nodeStates.set(id, "pruned");
    }

    // Services: add as children of selected leaf categories
    // A leaf category is selected + has services + no child categories in visibleIds
    for (const id of selectedCatIds) {
      const taxNode = taxMap.get(id);
      if (!taxNode?.services || taxNode.services.length === 0) continue;
      // Check it's a leaf in the visible tree (no visible child categories)
      const hasVisibleChildCat = taxNode.children.some((c) => visibleIds.has(c.id));
      if (hasVisibleChildCat) continue;

      for (const svcId of taxNode.services) {
        const svcNodeId = `svc:${svcId}`;
        visibleIds.add(svcNodeId);
        if (selectedServiceIds === null) {
          nodeStates.set(svcNodeId, "svc-pending");
        } else if (selectedServiceIds.has(svcId)) {
          nodeStates.set(svcNodeId, "svc-selected");
        } else {
          nodeStates.set(svcNodeId, "svc-rejected");
        }
      }
    }

    // Build filtered tree
    const buildNode = (taxNode: TaxonomyNode): TreeNode | null => {
      if (!visibleIds.has(taxNode.id)) return null;

      const catChildren = taxNode.children
        .map((c) => buildNode(c))
        .filter(Boolean) as TreeNode[];

      // Add service nodes as children of leaf categories
      const svcChildren: TreeNode[] = [];
      if (taxNode.services && selectedCatIds.has(taxNode.id)) {
        const hasVisibleChildCat = taxNode.children.some((c) => visibleIds.has(c.id));
        if (!hasVisibleChildCat) {
          for (const svcId of taxNode.services) {
            const svcNodeId = `svc:${svcId}`;
            if (visibleIds.has(svcNodeId)) {
              svcChildren.push({ id: svcNodeId, name: svcId, isService: true });
            }
          }
        }
      }

      const children = [...catChildren, ...svcChildren];
      return {
        id: taxNode.id,
        name: taxNode.name,
        children: children.length > 0 ? children : undefined,
      };
    };

    return { root: buildNode(taxonomyTree), nodeStates };
  }, [taxonomyTree, taxMap, trace, selectedServiceIds]);

  // D3 rendering
  useEffect(() => {
    if (!svgRef.current || !treeData.root) return;

    const { root, nodeStates } = treeData;
    const svg = d3.select(svgRef.current);
    const width = svgRef.current.clientWidth || 900;
    const height = 220;
    const margin = { top: 8, right: 20, bottom: 8, left: 20 };

    if (!initializedRef.current || !gRef.current) {
      svg.selectAll("*").remove();
      const g = svg.append("g").attr("transform", `translate(${margin.left},${margin.top})`);
      gRef.current = g.node();

      const zoom = d3.zoom<SVGSVGElement, unknown>()
        .scaleExtent([0.3, 3])
        .filter((event) => {
          if (event.type === "wheel") return true;
          return !event.ctrlKey && !event.button;
        })
        .on("zoom", (event) => d3.select(gRef.current).attr("transform", event.transform));
      svg.call(zoom);
      svgRef.current!.addEventListener("wheel", (e) => e.preventDefault(), { passive: false });
      zoomRef.current = zoom;
      initializedRef.current = true;
    }

    const g = d3.select(gRef.current!);
    const treeLayout = d3.tree<TreeNode>()
      .size([height - margin.top - margin.bottom, width - margin.left - margin.right]);

    const hierarchy = d3.hierarchy(root);
    const laid = treeLayout(hierarchy);

    const getStyle = (id: string) => {
      const state = nodeStates.get(id) ?? "default";
      switch (state) {
        case "selected":     return { fill: "#34d399", stroke: "#059669", r: 6, textFill: "#059669", fontWeight: "600" };
        case "pending":      return { fill: "#60a5fa", stroke: "#2563eb", r: 6, textFill: "#2563eb", fontWeight: "400" };
        case "pruned":       return { fill: "#fca5a5", stroke: "#ef4444", r: 5, textFill: "#ef4444", fontWeight: "400" };
        case "svc-selected": return { fill: "#34d399", stroke: "#059669", r: 4, textFill: "#059669", fontWeight: "600" };
        case "svc-rejected": return { fill: "#fca5a5", stroke: "#ef4444", r: 3, textFill: "#ef4444", fontWeight: "400" };
        case "svc-pending":  return { fill: "#60a5fa", stroke: "#2563eb", r: 4, textFill: "#2563eb", fontWeight: "400" };
        default:             return { fill: "#d4d4d8", stroke: "#a1a1aa", r: 5, textFill: "#71717a", fontWeight: "400" };
      }
    };

    const linkGen = d3.linkHorizontal<
      d3.HierarchyLink<TreeNode>,
      d3.HierarchyPointNode<TreeNode>
    >().x((d) => d.y).y((d) => d.x);

    // --- Links ---
    const links = g.selectAll<SVGPathElement, d3.HierarchyLink<TreeNode>>(".tree-link")
      .data(laid.links(), (d) => `${(d.source.data as TreeNode).id}-${(d.target.data as TreeNode).id}`);

    links.exit().transition().duration(200).attr("opacity", 0).remove();

    const linksEnter = links.enter().append("path")
      .attr("class", "tree-link")
      .attr("opacity", 0)
      .attr("d", linkGen as unknown as string);

    links.merge(linksEnter)
      .transition().duration(300)
      .attr("opacity", 1)
      .attr("d", linkGen as unknown as string)
      .attr("class", (d) => {
        const tid = (d.target.data as TreeNode).id;
        const state = nodeStates.get(tid);
        if (state === "selected" || state === "svc-selected") return "tree-link active";
        if (state === "pruned" || state === "svc-rejected") return "tree-link pruned";
        return "tree-link";
      });

    // --- Nodes ---
    const nodes = g.selectAll<SVGGElement, d3.HierarchyPointNode<TreeNode>>(".tree-node")
      .data(laid.descendants(), (d) => (d.data as TreeNode).id);

    nodes.exit().transition().duration(200).attr("opacity", 0).remove();

    const nodesEnter = nodes.enter().append("g")
      .attr("class", "tree-node")
      .attr("opacity", 0)
      .attr("transform", (d) => `translate(${d.y},${d.x})`);

    nodesEnter.append("circle").attr("class", "tree-node-circle");
    nodesEnter.append("text").attr("dy", "0.31em");

    const merged = nodes.merge(nodesEnter);

    merged.transition().duration(300)
      .attr("opacity", 1)
      .attr("transform", (d) => `translate(${d.y},${d.x})`);

    merged.select("circle")
      .transition().duration(300)
      .attr("r", (d) => getStyle((d.data as TreeNode).id).r)
      .attr("fill", (d) => getStyle((d.data as TreeNode).id).fill)
      .attr("stroke", (d) => getStyle((d.data as TreeNode).id).stroke)
      .attr("stroke-width", 1.5);

    merged.select("text")
      .attr("x", (d) => (d.children ? -10 : 10))
      .attr("text-anchor", (d) => (d.children ? "end" : "start"))
      .attr("font-size", (d) => (d.data as TreeNode).isService ? "8px" : "10px")
      .attr("fill", (d) => getStyle((d.data as TreeNode).id).textFill)
      .attr("font-weight", (d) => getStyle((d.data as TreeNode).id).fontWeight)
      .text((d) => {
        const n = d.data as TreeNode;
        const name = n.isService ? n.name.replace(/^svc:/, "") : n.name;
        return name.length > 28 ? name.slice(0, 25) + "..." : name;
      });

    // Auto-fit only when tree structure changes (new nodes added/removed)
    const currentNodeCount = laid.descendants().length;
    if (currentNodeCount !== prevNodeCountRef.current) {
      prevNodeCountRef.current = currentNodeCount;
      setTimeout(() => {
        if (!gRef.current || !zoomRef.current || !svgRef.current) return;
        const bounds = gRef.current.getBBox();
        if (bounds.width <= 0) return;
        const pad = 30;
        const fw = bounds.width + pad * 2;
        const fh = bounds.height + pad * 2;
        const currentWidth = svgRef.current.clientWidth || width;
        const scale = Math.min(currentWidth / fw, height / fh, 1.2) * 0.9;
        const tx = (currentWidth - bounds.width * scale) / 2 - bounds.x * scale;
        const ty = (height - bounds.height * scale) / 2 - bounds.y * scale;
        svg.transition().duration(300).call(
          zoomRef.current!.transform,
          d3.zoomIdentity.translate(tx, ty).scale(scale),
        );
      }, 350);
    }
  }, [treeData]);

  useEffect(() => {
    initializedRef.current = false;
  }, [taxonomyTree]);

  return (
    <div className="rounded-lg border border-zinc-200/80 bg-white overflow-hidden">
      <div className="flex items-center justify-between px-3 py-1 border-b border-zinc-100 bg-zinc-50">
        <span className="text-[11px] font-semibold text-zinc-500">分类树导航</span>
        <div className="flex items-center gap-3 text-[10px] text-zinc-400">
          <span className="flex items-center gap-1"><span className="w-2 h-2 rounded-full bg-emerald-400" /> 选中</span>
          <span className="flex items-center gap-1"><span className="w-2 h-2 rounded-full bg-blue-400" /> 评估中</span>
          <span className="flex items-center gap-1"><span className="w-2 h-2 rounded-full bg-red-300" /> 剪枝</span>
        </div>
      </div>
      <svg ref={svgRef} className="w-full" style={{ height: "220px" }} />
    </div>
  );
}
