function decodeCreationOptions(publicKey) {
  publicKey.challenge = b64urlToBuffer(publicKey.challenge);
  publicKey.user.id = b64urlToBuffer(publicKey.user.id);
  for (const item of publicKey.excludeCredentials || []) item.id = b64urlToBuffer(item.id);
  return publicKey;
}

document.getElementById('register').onclick = async function() {
  const status = document.getElementById('status');
  try {
    status.textContent = 'Waiting for browser authenticator...';
    const credential = await navigator.credentials.create({
      publicKey: decodeCreationOptions(JSON.parse(this.dataset.options)),
    });
    const response = await fetch('/register/complete', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({
        registrationRequestId: this.dataset.requestId,
        credential: credentialToJSON(credential),
      }),
    });
    const result = await response.json();
    if (!response.ok || result.ok === false) throw new Error(result.message || 'Registration failed');
    status.textContent = result.message;
  } catch (error) {
    status.textContent = error.message;
  }
};
