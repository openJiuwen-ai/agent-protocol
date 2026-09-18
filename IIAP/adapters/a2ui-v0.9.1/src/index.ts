import type {
  ComponentObservation, InteractionCapability, ObservationPlan, ProtocolInput,
  SafeSuggestion, SuggestionContext, SuggestionPolicy, SurfaceContext,
  UIProtocolAdapter, UpdateValue,
} from '@openjiuwen/iiap';
import { validateDataModelSuggestion } from '@openjiuwen/iiap';

export type A2UIV091Message = Record<string, unknown>;
export type CatalogCapabilityMap = Record<string, InteractionCapability>;
const STANDARD: CatalogCapabilityMap = {
  TextField: 'text_edit', CheckBox: 'boolean_toggle', Slider: 'scalar_adjust',
  DateTimeInput: 'temporal_edit', MultipleChoice: 'option_select', Button: 'action_invoke',
  Tabs: 'content_navigate', Modal: 'overlay_reveal', Video: 'media_control',
  AudioPlayer: 'media_control', List: 'viewport_navigate',
};
const record = (value: unknown): value is Record<string, unknown> => Boolean(value) && typeof value === 'object' && !Array.isArray(value);
const refPath = (value: unknown): string | undefined => record(value) && typeof value.path === 'string' ? value.path : undefined;
const STANDARD_DEFINITION_TYPES = new Set([
  ...Object.keys(STANDARD), 'Column', 'Row', 'List', 'Card', 'Text', 'Image',
  'Divider', 'Icon', 'Link', 'Spacer', 'Grid', 'Form', 'Switch', 'RadioGroup',
]);
function sanitizeDefinitionValue(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(sanitizeDefinitionValue);
  if (!record(value)) return value;
  return Object.fromEntries(Object.entries(value)
    .filter(([key]) => key !== 'context')
    .map(([key, child]) => [key, sanitizeDefinitionValue(child)]));
}
function referencedComponentIds(value: unknown, ids: Set<string>, result = new Set<string>()): Set<string> {
  if (typeof value === 'string' && ids.has(value)) result.add(value);
  else if (Array.isArray(value)) value.forEach((item) => referencedComponentIds(item, ids, result));
  else if (record(value)) Object.values(value).forEach((item) => referencedComponentIds(item, ids, result));
  return result;
}
function safeAllowedValues(value: unknown): UpdateValue[] | undefined {
  if (!Array.isArray(value)) return undefined;
  const result: UpdateValue[] = [];
  for (const item of value) {
    if (!(item === null || ['string', 'boolean'].includes(typeof item)
      || (typeof item === 'number' && Number.isFinite(item)))) continue;
    if (!result.some((existing) => existing === item && (existing === null || typeof existing === typeof item))) result.push(item as UpdateValue);
  }
  return result.slice(0, 16);
}

export class A2UIV091Adapter implements UIProtocolAdapter<A2UIV091Message, A2UIV091Message> {
  private readonly customCatalog: CatalogCapabilityMap;
  private readonly suggestionPolicy?: SuggestionPolicy;

  constructor(customCatalog: CatalogCapabilityMap = {}, suggestionPolicy?: SuggestionPolicy) {
    this.customCatalog = customCatalog;
    this.suggestionPolicy = suggestionPolicy;
  }
  buildObservationPlans(input: ProtocolInput<A2UIV091Message>): ObservationPlan[] {
    const surfaces = new Map<string, { originalSurfaceId: string; surfaceInstanceId: string }>();
    const components: ComponentObservation[] = [];
    const actions: ObservationPlan['explicitActionBoundaries'] = [];
    for (const message of input.messages) {
      const deleted = record(message.deleteSurface) && typeof message.deleteSurface.surfaceId === 'string'
        ? message.deleteSurface.surfaceId : null;
      if (deleted) {
        surfaces.delete(deleted);
        for (let index = components.length - 1; index >= 0; index -= 1) {
          if (components[index]?.surfaceId === deleted) components.splice(index, 1);
        }
        for (let index = actions.length - 1; index >= 0; index -= 1) {
          if (actions[index]?.surfaceId === deleted) actions.splice(index, 1);
        }
        continue;
      }
      const lifecycle = [message.createSurface, message.updateComponents, message.updateDataModel]
        .find((item) => record(item) && typeof item.surfaceId === 'string') as Record<string, unknown> | undefined;
      if (lifecycle) {
        const id = lifecycle.surfaceId as string;
        surfaces.set(id, { originalSurfaceId: id, surfaceInstanceId: `${input.namespace}:${id}` });
      }
      const update = record(message.updateComponents) ? message.updateComponents : null;
      if (!update || typeof update.surfaceId !== 'string' || !Array.isArray(update.components)) continue;
      for (const node of update.components) {
        if (!record(node) || typeof node.id !== 'string') continue;
        const parts = this.componentParts(node.component);
        if (!parts) continue;
        const [type, props] = parts;
        const capability = this.customCatalog[type] ?? STANDARD[type];
        if (!capability) continue;
        const surfaceInstanceId = `${input.namespace}:${update.surfaceId}`;
        const mode = this.selectionMode(type, props);
        const maxAllowedSelections = type === 'MultipleChoice' ? this.explicitSelectionLimit(props) : undefined;
        const bindingPath = this.binding(type, props);
        const declared = this.declaredValues(type, props);
        const policyValues = bindingPath && (type !== 'MultipleChoice' || maxAllowedSelections !== undefined) ? this.suggestionPolicy?.resolveAllowedValues({
          originalSurfaceId: update.surfaceId, componentId: node.id, componentType: type,
          bindingPath, ...(mode ? { selectionMode: mode } : {}),
          ...(maxAllowedSelections !== undefined ? { maxAllowedSelections } : {}),
          ...(declared ? { declaredValues: declared } : {}),
        }) : undefined;
        const allowedValues = safeAllowedValues(policyValues);
        components.push({
          componentId: node.id, componentType: type, capability,
          componentRole: this.role(capability), surfaceId: update.surfaceId, surfaceInstanceId,
          selectionMode: mode, bindingPath,
          ...(maxAllowedSelections !== undefined ? { maxAllowedSelections } : {}),
          ...(allowedValues?.length ? { allowedValues } : {}),
        });
        if (capability === 'action_invoke' && record(props.action) && typeof props.action.name === 'string') {
          actions.push({ componentId: node.id, actionName: props.action.name, surfaceId: update.surfaceId, surfaceInstanceId });
        }
      }
    }
    return [...surfaces.values()].map((surface) => {
      const context = this.surfaceContext(input.messages, surface.originalSurfaceId);
      const update = context.definition.find((message) => record(message.updateComponents))?.updateComponents;
      const contextComponents = record(update) && Array.isArray(update.components) ? update.components : [];
      const includedIds = new Set(contextComponents.filter(record).map((node) => node.id).filter((id): id is string => typeof id === 'string'));
      const effectiveComponents = new Map(components
        .filter((component) => component.surfaceInstanceId === surface.surfaceInstanceId && includedIds.has(component.componentId))
        .map((component) => [component.componentId, component]));
      const effectiveActions = new Map(actions
        .filter((action) => action.surfaceInstanceId === surface.surfaceInstanceId && includedIds.has(action.componentId))
        .map((action) => [action.componentId, action]));
      return {
        sessionId: input.sessionId, messageId: input.messageId, protocolVersion: '0.9.1' as const, surface,
        surfaceContext: context,
        components: [...effectiveComponents.values()],
        explicitActionBoundaries: [...effectiveActions.values()],
      };
    }).filter((plan) => plan.components.length > 0);
  }
  private surfaceContext(messages: A2UIV091Message[], surfaceId: string): SurfaceContext {
    let root: string | undefined;
    let catalogId: string | undefined;
    const nodes = new Map<string, Record<string, unknown>>();
    for (const message of messages) {
      const deleted = record(message.deleteSurface) ? message.deleteSurface : null;
      if (deleted?.surfaceId === surfaceId) {
        root = undefined;
        catalogId = undefined;
        nodes.clear();
        continue;
      }
      const create = record(message.createSurface) ? message.createSurface : null;
      if (create?.surfaceId === surfaceId) {
        if (typeof create.root === 'string') root = create.root;
        if (typeof create.catalogId === 'string') catalogId = create.catalogId;
      }
      const update = record(message.updateComponents) ? message.updateComponents : null;
      if (update?.surfaceId !== surfaceId || !Array.isArray(update.components)) continue;
      for (const node of update.components) if (record(node) && typeof node.id === 'string') nodes.set(node.id, node);
    }
    const allIds = new Set(nodes.keys());
    const reachable = new Set<string>();
    if (root && nodes.has(root)) {
      const queue = [root];
      while (queue.length) {
        const id = queue.shift()!;
        if (reachable.has(id)) continue;
        reachable.add(id);
        const node = nodes.get(id);
        if (node) queue.push(...referencedComponentIds(node.component, allIds));
      }
    } else allIds.forEach((id) => reachable.add(id));
    const components = [...reachable].map((id) => nodes.get(id)!).map((node) => {
      const parts = this.componentParts(node.component);
      if (!parts) return { id: node.id, component: {} };
      const [type, props] = parts;
      const safeProperties = STANDARD_DEFINITION_TYPES.has(type) ? sanitizeDefinitionValue(props) : {};
      return { id: node.id, component: { type, properties: safeProperties } };
    });
    let truncated = components.length > 128;
    const boundedComponents = components.slice(0, 128);
    while (boundedComponents.length > 1 && JSON.stringify(boundedComponents).length > 16_384) {
      boundedComponents.pop();
      truncated = true;
    }
    return {
      protocol: 'a2ui', protocolVersion: '0.9.1', snapshotType: 'sanitized_effective_definition',
      definition: [
        { createSurface: { surfaceId, ...(root ? { root } : {}), ...(catalogId ? { catalogId } : {}) } },
        { updateComponents: { surfaceId, components: boundedComponents } },
      ],
      redaction: {
        dataModelExcluded: true, actionContextValuesExcluded: true,
        unreachableComponentsExcluded: Boolean(root), unknownCustomPropertiesExcluded: false,
        truncated,
      },
    };
  }
  validateSuggestion(value: unknown, context: SuggestionContext): SafeSuggestion<A2UIV091Message> | null {
    const safe = validateDataModelSuggestion(value, context);
    return safe ? { kind: 'data_model_update', updates: safe.updates.map((item) => ({ updateDataModel: { surfaceId: item.surfaceId, path: item.path, value: item.value } })) } : null;
  }
  private componentParts(value: unknown): [string, Record<string, unknown>] | null {
    if (!record(value)) return null;
    if (typeof value.type === 'string') return [value.type, record(value.properties) ? value.properties : value];
    const type = Object.keys(value)[0];
    return type ? [type, record(value[type]) ? value[type] as Record<string, unknown> : {}] : null;
  }
  private binding(type: string, props: Record<string, unknown>): string | undefined {
    if (type === 'TextField') return refPath(props.text) ?? refPath(props.value);
    if (['CheckBox', 'Slider', 'DateTimeInput'].includes(type)) return refPath(props.value);
    if (type === 'MultipleChoice') return refPath(props.selections) ?? refPath(props.value);
    return typeof props.bindingPath === 'string' ? props.bindingPath : undefined;
  }
  private declaredValues(type: string, props: Record<string, unknown>): UpdateValue[] | undefined {
    if (Array.isArray(props.suggestionValues)) return props.suggestionValues.filter((value): value is UpdateValue => value === null || ['string', 'number', 'boolean'].includes(typeof value)).slice(0, 16);
    if (type === 'MultipleChoice' && Array.isArray(props.options)) return props.options.map((item) => record(item) ? item.value : undefined).filter((value): value is UpdateValue => value === null || ['string', 'number', 'boolean'].includes(typeof value)).slice(0, 16);
    return undefined;
  }
  private selectionMode(type: string, props: Record<string, unknown>): ComponentObservation['selectionMode'] {
    if (type !== 'MultipleChoice') return undefined;
    const variant = typeof props.variant === 'string' ? props.variant : props.type;
    const maxSelections = this.explicitSelectionLimit(props);
    if (maxSelections !== undefined) return maxSelections > 1 ? 'multiple' : 'single';
    return variant === 'chips' || variant === 'checkbox' ? 'multiple' : 'single';
  }
  private explicitSelectionLimit(props: Record<string, unknown>): number | undefined {
    const value = props.maxAllowedSelections;
    return typeof value === 'number' && Number.isInteger(value) && value >= 1 ? value : undefined;
  }
  private role(capability: InteractionCapability): string {
    if (capability === 'action_invoke') return 'action';
    if (['content_navigate', 'overlay_reveal', 'viewport_navigate'].includes(capability)) return 'container';
    if (capability === 'media_control') return 'media';
    return 'input';
  }
}
