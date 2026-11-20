function b64urlToBuffer(value) {
  const base64 = value.replace(/-/g, '+').replace(/_/g, '/');
  const padded = base64 + '='.repeat((4 - base64.length % 4) % 4);
  const binary = atob(padded);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes.buffer;
}

function bufferToB64url(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

function credentialToJSON(credential) {
  const response = credential.response;
  const out = {
    id: credential.id,
    rawId: bufferToB64url(credential.rawId),
    type: credential.type,
    response: {},
  };
  for (const key of ['attestationObject', 'authenticatorData', 'clientDataJSON', 'signature', 'userHandle']) {
    const value = response[key];
    if (value instanceof ArrayBuffer) out.response[key] = bufferToB64url(value);
  }
  if (typeof response.getTransports === 'function') out.response.transports = response.getTransports();
  return out;
}
