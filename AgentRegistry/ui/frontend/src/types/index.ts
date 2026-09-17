export interface ServiceResult {
  id: string;
  name: string;
  description: string;
}

export interface NavigationStep {
  parent_id: string;
  selected: string[];   // selected child category IDs
  pruned: string[];     // pruned child category IDs
}

export interface SearchTrace {
  navigation_steps: NavigationStep[];
}

export interface SearchStats {
  llm_calls: number;
  total_tokens: number;
  visited_categories?: number;
  pruned_categories?: number;
}

export interface SearchResponse {
  results: ServiceResult[];
  stats: SearchStats;
  elapsed_time: number;
}

export interface DatasetInfo {
  name: string;
  service_count: number;
  query_count: number;
}

export interface DefaultQuery {
  query: string;
  query_en: string;
}

export type SearchMethod =
  | "a2x_get_all"
  | "a2x_get_one"
  | "a2x_get_important"
  | "vector"
  | "traditional";

export interface TaxonomyNode {
  id: string;
  name: string;
  description: string;
  service_count: number;
  children: TaxonomyNode[];
  services?: string[];  // leaf categories: service IDs for tree animation
}
