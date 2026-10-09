function decodeRequestOptions(publicKey) {
  publicKey.challenge = b64urlToBuffer(publicKey.challenge);
  for (const item of publicKey.allowCredentials || []) item.id = b64urlToBuffer(item.id);
  return publicKey;
}

async function postJSON(path, payload) {
  const response = await fetch(path, {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload),
  });
  const data = await response.json();
  if (!response.ok || data.ok === false) throw new Error(data.message || data.error || 'Request failed');
  return data;
}

async function approveWithPasskey(button) {
  const article = button.closest('article');
  const status = article.querySelector('.status');
  try {
    status.textContent = 'Waiting for browser authentication...';
    const authorizationId = article.dataset.authorizationId;
    const options = JSON.parse(article.dataset.signingOptions);
    if (options.signatureMethod !== 'webauthn') throw new Error('Unsupported signature method');
    const assertion = await navigator.credentials.get({
      publicKey: decodeRequestOptions(options.methodOptions),
    });
    const result = await postJSON('/approve', {
      authorizationId,
      assertion: credentialToJSON(assertion),
    });
    status.textContent = result.message;
  } catch (error) {
    status.textContent = error.message;
  }
}
