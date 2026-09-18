import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { IIAPHelpHost, ReactHelpPresenter } from '@openjiuwen/iiap/react';
import type { IntentContextPacket } from '@openjiuwen/iiap';

const packet: IntentContextPacket = {
  iiapVersion: '0.1', packetId: 'demo-packet', sessionId: 'demo-session', messageId: 'demo-message',
  originalSurfaceId: 'main', surfaceInstanceId: 'demo-message:main', protocolVersion: '0.9.1',
  timestamp: new Date(0).toISOString(),
  window: { startTime: new Date(0).toISOString(), endTime: new Date(1000).toISOString(), durationMs: 1000 },
  surfaceContext: { protocol: 'a2ui', protocolVersion: '0.9.1', snapshotType: 'sanitized_effective_definition', definition: [], redaction: { dataModelExcluded: true, actionContextValuesExcluded: true, unreachableComponentsExcluded: false, unknownCustomPropertiesExcluded: true, truncated: false } },
  observations: { tokenScope: 'component_within_surface_instance', events: [], completeness: { complete: true, droppedEventCount: 0 } },
  patterns: [], reportHistory: [], allowedOperations: { updateTargets: [] },
};

function Demo() {
  const presenter = useMemo(() => new ReactHelpPresenter(), []);
  const [response, setResponse] = useState('等待响应');
  useEffect(() => {
    void presenter.present({
      decision: 'offer_help', reason: 'comparison_need', offerType: 'text_assistance',
      helpTopic: 'compare_options', uiStyle: 'inline_card', message: '我可以帮你梳理这些选项。',
    }, { packet, decisionId: 'demo-decision' }).then(setResponse);
  }, [presenter]);
  return <main><h1>IIAP Help Presenter Demo</h1><IIAPHelpHost presenter={presenter}/><output data-testid="feedback">{response}</output></main>;
}

createRoot(document.getElementById('root')!).render(<Demo/>);
