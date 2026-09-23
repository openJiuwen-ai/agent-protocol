import type {
  ComponentObservation, InteractionCapability, ObservationPlan, ProtocolInput,
  SafeDataModelUpdate, SafeSuggestion, SuggestionContext, SuggestionPolicy,
  SurfaceContext, UIProtocolAdapter, UpdateValue,
} from '@openjiuwen/iiap';
import { validateDataModelSuggestion } from '@openjiuwen/iiap';

export type A2UIV08Message = Record<string, unknown>;
const CAPABILITIES: Record<string, InteractionCapability> = {
  TextField: 'text_edit', CheckBox: 'boolean_toggle', Slider: 'scalar_adjust',
  DateTimeInput: 'temporal_edit', MultipleChoice: 'option_select', Button: 'action_invoke',
  Tabs: 'content_navigate', Modal: 'overlay_reveal', Video: 'media_control',
  AudioPlayer: 'media_control', List: 'viewport_navigate',
};
const record = (value: unknown): value is Record<string, unknown> => Boolean(value) && typeof value === 'object' && !Array.isArray(value);
const refPath = (value: unknown): string | undefined => record(value) && typeof value.path === 'string' ? value.path : undefined;
const STANDARD_DEFINITION_TYPES = new Set([
  ...Object.keys(CAPABILITIES), 'Column', 'Row', 'List', 'Card', 'Text', 'Image',
  'Divider', 'Icon',
]);

const COMPONENT_PROPERTY_KEYS: Record<string, readonly string[]> = {
  Text: ['text', 'usageHint'], Image: ['url', 'usageHint', 'fit'], Icon: ['name'],
  Video: ['url'], AudioPlayer: ['url', 'description'],
  Row: ['children', 'distribution', 'alignment'], Column: ['children', 'distribution', 'alignment'],
  List: ['children', 'direction', 'alignment'], Card: ['child', 'children'], Tabs: ['tabItems'],
  Divider: ['axis', 'color', 'thickness'], Modal: ['entryPointChild', 'contentChild'],
  Button: ['child', 'action'], CheckBox: ['label', 'value'],
  TextField: ['text', 'label', 'type', 'validationRegexp'],
  DateTimeInput: ['value', 'enableDate', 'enableTime', 'outputFormat'],
  MultipleChoice: ['selections', 'options', 'maxAllowedSelections', 'type'],
  Slider: ['value', 'minValue', 'maxValue'],
};

function pick(value: unknown, keys: readonly string[]): Record<string, unknown> {
  if (!record(value)) return {};
  return Object.fromEntries(keys.filter((key) => value[key] !== undefined).map((key) => [key, value[key]]));
}

function sanitizeReference(value: unknown): Record<string, unknown> {
  const candidate = pick(value, ['path', 'literal', 'literalString', 'literalNumber', 'literalBoolean', 'literalArray']);
  return Object.fromEntries(Object.entries(candidate).filter(([, item]) => item === null
    || ['string', 'number', 'boolean'].includes(typeof item)
    || (Array.isArray(item) && item.every((entry) => ['string', 'number', 'boolean'].includes(typeof entry)))));
}

function sanitizeDefinitionValue(key: string, value: unknown): unknown {
  if (['text', 'label', 'value', 'url', 'name', 'description', 'title', 'selections'].includes(key)) {
    return sanitizeReference(value);
  }
  if (key === 'action') return pick(value, ['name']);
  if (key === 'children') {
    const children = pick(value, ['explicitList', 'template']);
    return {
      ...(Array.isArray(children.explicitList)
        ? { explicitList: children.explicitList.filter((item): item is string => typeof item === 'string') }
        : {}),
      ...(record(children.template) ? { template: pick(children.template, ['componentId', 'dataBinding']) } : {}),
    };
  }
  if (key === 'options' && Array.isArray(value)) {
    return value.filter(record).map((option) => ({
      ...(record(option.label) ? { label: sanitizeReference(option.label) } : {}),
      ...(option.value === null || ['string', 'number', 'boolean'].includes(typeof option.value)
        ? { value: option.value } : {}),
    }));
  }
  if (key === 'tabItems' && Array.isArray(value)) {
    return value.filter(record).map((item) => ({
      ...(record(item.title) ? { title: sanitizeReference(item.title) } : {}),
      ...(typeof item.child === 'string' ? { child: item.child } : {}),
    }));
  }
  return value === null || ['string', 'number', 'boolean'].includes(typeof value) ? value : undefined;
}

function sanitizeComponentProperties(type: string, props: Record<string, unknown>): Record<string, unknown> {
  const keys = COMPONENT_PROPERTY_KEYS[type] ?? [];
  return Object.fromEntries(keys
    .map((key) => [key, sanitizeDefinitionValue(key, props[key])] as const)
    .filter((entry): entry is readonly [string, unknown] => entry[1] !== undefined));
}

function referencedComponentIds(value: unknown, ids: Set<string>, result = new Set<string>()): Set<string> {
  if (typeof value === 'string' && ids.has(value)) result.add(value);
  else if (Array.isArray(value)) value.forEach((item) => referencedComponentIds(item, ids, result));
  else if (record(value)) Object.values(value).forEach((item) => referencedComponentIds(item, ids, result));
  return result;
}

function surfaceContext(messages: A2UIV08Message[], surfaceId: string): SurfaceContext {
  let root: string | undefined;
  const nodes = new Map<string, Record<string, unknown>>();
  for (const message of messages) {
    const deleted = record(message.deleteSurface) ? message.deleteSurface : null;
    if (deleted?.surfaceId === surfaceId) {
      root = undefined;
      nodes.clear();
      continue;
    }
    const begin = record(message.beginRendering) ? message.beginRendering : null;
    if (begin?.surfaceId === surfaceId && typeof begin.root === 'string') root = begin.root;
    const update = record(message.surfaceUpdate) ? message.surfaceUpdate : null;
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
  } else {
    allIds.forEach((id) => reachable.add(id));
  }
  const components = [...reachable].map((id) => nodes.get(id)!).map((node) => {
    const parts = componentParts(node.component);
    if (!parts) return { id: node.id, component: {} };
    const [type, props] = parts;
    return {
      id: node.id,
      component: { [type]: STANDARD_DEFINITION_TYPES.has(type) ? sanitizeComponentProperties(type, props) : {} },
    };
  });
  let truncated = components.length > 128;
  const boundedComponents = components.slice(0, 128);
  while (boundedComponents.length > 1 && JSON.stringify(boundedComponents).length > 16_384) {
    boundedComponents.pop();
    truncated = true;
  }
  return {
    protocol: 'a2ui', protocolVersion: '0.8', snapshotType: 'sanitized_effective_definition',
    definition: [
      { beginRendering: { surfaceId, ...(root ? { root } : {}) } },
      { surfaceUpdate: { surfaceId, components: boundedComponents } },
    ],
    redaction: {
      dataModelExcluded: true, actionContextValuesExcluded: true,
      unreachableComponentsExcluded: Boolean(root), unknownCustomPropertiesExcluded: true,
      truncated,
    },
  };
}

function contextComponentIds(context: SurfaceContext, messageKey: 'surfaceUpdate'): Set<string> {
  const update = context.definition.find((message) => record(message[messageKey]))?.[messageKey];
  const components = record(update) && Array.isArray(update.components) ? update.components : [];
  return new Set(components.filter(record).map((node) => node.id).filter((id): id is string => typeof id === 'string'));
}

export function capabilityForV08Component(type: string): InteractionCapability | undefined { return CAPABILITIES[type]; }
function role(capability: InteractionCapability): string {
  if (capability === 'action_invoke') return 'action';
  if (['content_navigate', 'overlay_reveal', 'viewport_navigate'].includes(capability)) return 'container';
  if (capability === 'media_control') return 'media';
  return 'input';
}
function binding(type: string, props: Record<string, unknown>): string | undefined {
  if (type === 'TextField') return refPath(props.text);
  if (['CheckBox', 'Slider', 'DateTimeInput'].includes(type)) return refPath(props.value);
  if (type === 'MultipleChoice') return refPath(props.selections) ?? refPath(props.value);
  return undefined;
}
function declaredValues(type: string, props: Record<string, unknown>): UpdateValue[] | undefined {
  // 更新值必须能对着 host 声明的有限集校验，否则执行边界只能"相信模型"，等于允许凭空编造
  // UI 事实。这里只接受协议显式枚举出值域的组件，不猜测、不推导：
  // - MultipleChoice：options 的 value 就是全部合法取值。
  // - CheckBox：A2UI 0.8 的 CheckBox 是无参数布尔开关，值域恒为 {true, false}，与 options
  //   等价地有限且可逆。（协议里没有三态或多态开关。）
  // TextField / DateTimeInput 值域无界，Slider 只有 minValue/maxValue 且协议无 step（连续），
  // 都无法枚举，因此不授权——这不是保守闸门，而是"没有可校验的合法值"。
  if (type === 'CheckBox') return [true, false];
  if (type === 'MultipleChoice' && Array.isArray(props.options)) {
    const values = props.options.map((item) => record(item) ? item.value : undefined)
      .filter((item): item is UpdateValue => item === null || ['string', 'number', 'boolean'].includes(typeof item));
    return values.slice(0, 16);
  }
  return undefined;
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
function selectionMode(type: string, props: Record<string, unknown>): ComponentObservation['selectionMode'] {
  if (type !== 'MultipleChoice') return undefined;
  const variant = typeof props.variant === 'string' ? props.variant : props.type;
  const maxSelections = explicitSelectionLimit(props);
  if (maxSelections !== undefined) return maxSelections > 1 ? 'multiple' : 'single';
  return variant === 'chips' || variant === 'checkbox' ? 'multiple' : 'single';
}
function explicitSelectionLimit(props: Record<string, unknown>): number | undefined {
  const value = props.maxAllowedSelections;
  return typeof value === 'number' && Number.isInteger(value) && value >= 1 ? value : undefined;
}
function componentParts(value: unknown): [string, Record<string, unknown>] | null {
  if (!record(value)) return null;
  const type = Object.keys(value)[0];
  return type ? [type, record(value[type]) ? value[type] as Record<string, unknown> : {}] : null;
}

export class A2UIV08Adapter implements UIProtocolAdapter<A2UIV08Message, A2UIV08Message> {
  private readonly suggestionPolicy?: SuggestionPolicy;

  constructor(suggestionPolicy?: SuggestionPolicy) {
    this.suggestionPolicy = suggestionPolicy;
  }

  buildObservationPlans(input: ProtocolInput<A2UIV08Message>): ObservationPlan[] {
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
      const update = record(message.surfaceUpdate) ? message.surfaceUpdate : null;
      const lifecycle = [message.beginRendering, message.surfaceUpdate, message.dataModelUpdate]
        .find((item) => record(item) && typeof item.surfaceId === 'string') as Record<string, unknown> | undefined;
      if (lifecycle) {
        const surfaceId = lifecycle.surfaceId as string;
        surfaces.set(surfaceId, { originalSurfaceId: surfaceId, surfaceInstanceId: `${input.namespace}:${surfaceId}` });
      }
      if (!update || typeof update.surfaceId !== 'string' || !Array.isArray(update.components)) continue;
      for (const node of update.components) {
        if (!record(node) || typeof node.id !== 'string') continue;
        const parts = componentParts(node.component);
        if (!parts) continue;
        const [type, props] = parts;
        const capability = CAPABILITIES[type];
        if (!capability) continue;
        const surfaceInstanceId = `${input.namespace}:${update.surfaceId}`;
        const mode = selectionMode(type, props);
        const maxAllowedSelections = type === 'MultipleChoice' ? explicitSelectionLimit(props) : undefined;
        const bindingPath = binding(type, props);
        const declared = declaredValues(type, props);
        // 授权只看两件事：组件是否绑定到 data model，以及策略是否为它给出可校验的有限取值。
        // 选择基数（maxAllowedSelections）是值的形状约束，不再是授权前置条件：未声明时
        // 多选字段仍可授权，只是不做集合上限比较。没有策略返回值的字段一律不授权。
        const policyValues = bindingPath ? this.suggestionPolicy?.resolveAllowedValues({
          originalSurfaceId: update.surfaceId, componentId: node.id, componentType: type,
          bindingPath, ...(mode ? { selectionMode: mode } : {}),
          ...(maxAllowedSelections !== undefined ? { maxAllowedSelections } : {}),
          ...(declared ? { declaredValues: declared } : {}),
        }) : undefined;
        const allowedValues = safeAllowedValues(policyValues);
        components.push({
          componentId: node.id, componentType: type, capability, componentRole: role(capability),
          surfaceId: update.surfaceId, surfaceInstanceId,
          selectionMode: mode, bindingPath,
          ...(maxAllowedSelections !== undefined ? { maxAllowedSelections } : {}),
          ...(allowedValues?.length ? { allowedValues } : {}),
        });
        if (type === 'Button' && record(props.action) && typeof props.action.name === 'string') {
          actions.push({ componentId: node.id, actionName: props.action.name, surfaceId: update.surfaceId, surfaceInstanceId });
        }
      }
    }
    return [...surfaces.values()].map((surface) => {
      const context = surfaceContext(input.messages, surface.originalSurfaceId);
      const includedIds = contextComponentIds(context, 'surfaceUpdate');
      const effectiveComponents = new Map(components
        .filter((component) => component.surfaceInstanceId === surface.surfaceInstanceId && includedIds.has(component.componentId))
        .map((component) => [component.componentId, component]));
      const effectiveActions = new Map(actions
        .filter((action) => action.surfaceInstanceId === surface.surfaceInstanceId && includedIds.has(action.componentId))
        .map((action) => [action.componentId, action]));
      return {
        sessionId: input.sessionId, messageId: input.messageId, protocolVersion: '0.8' as const, surface,
        surfaceContext: context,
        components: [...effectiveComponents.values()],
        explicitActionBoundaries: [...effectiveActions.values()],
      };
    }).filter((plan) => plan.components.length > 0);
  }
  validateSuggestion(value: unknown, context: SuggestionContext): SafeSuggestion<A2UIV08Message> | null {
    const safe = validateDataModelSuggestion(value, context);
    return safe ? { kind: 'data_model_update', updates: safe.updates.map((item) => ({ dataModelUpdate: { surfaceId: item.surfaceId, path: item.path, contents: [{ key: '.', ...valueField(item.value) }] } })) } : null;
  }
}

function valueField(value: SafeDataModelUpdate['value']): Record<string, unknown> {
  if (Array.isArray(value)) return { valueString: JSON.stringify(value) };
  if (typeof value === 'string') return { valueString: value };
  if (typeof value === 'number') return { valueNumber: value };
  if (typeof value === 'boolean') return { valueBoolean: value };
  return { valueNull: true };
}
