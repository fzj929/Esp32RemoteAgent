async function request(path, options = {}) {
  const response = await fetch(path, {
    credentials: 'same-origin',
    ...options,
    headers: {
      ...(options.body ? { 'Content-Type': 'application/json' } : {}),
      ...(options.headers || {})
    }
  });

  if (!response.ok) {
    const body = await response.json().catch(() => ({}));
    const error = new Error(body.error || `HTTP ${response.status}`);
    error.status = response.status;
    throw error;
  }

  return response.status === 204 ? null : response.json().catch(() => null);
}

export const api = {
  status: () => request('/api/auth/status'),
  login: (payload) => request('/api/auth/login', { method: 'POST', body: JSON.stringify(payload) }),
  logout: () => request('/api/auth/logout', { method: 'POST' }),
  changePassword: (payload) => request('/api/auth/change-password', { method: 'POST', body: JSON.stringify(payload) }),
  users: () => request('/api/auth/users'),
  createUser: (payload) => request('/api/auth/users', { method: 'POST', body: JSON.stringify(payload) }),
  updateUser: (username, payload) => request(`/api/auth/users/${encodeURIComponent(username)}`, { method: 'PUT', body: JSON.stringify(payload) }),
  resetPassword: (username, payload) => request(`/api/auth/users/${encodeURIComponent(username)}/reset-password`, { method: 'POST', body: JSON.stringify(payload) }),
  boards: () => request('/api/boards'),
  diagnostics: () => request('/api/boards/diagnostics'),
  events: () => request('/api/events'),
  publicPorts: () => request('/api/public-ports'),
  createPublicPort: (payload) => request('/api/public-ports', { method: 'POST', body: JSON.stringify(payload) }),
  updatePublicPort: (publicPort, payload) => request(`/api/public-ports/${publicPort}`, { method: 'PUT', body: JSON.stringify(payload) }),
  deletePublicPort: (publicPort) => request(`/api/public-ports/${publicPort}`, { method: 'DELETE' }),
  createBoard: (payload) => request('/api/boards', { method: 'POST', body: JSON.stringify(payload) }),
  updateBoard: (boardId, payload) => request(`/api/boards/${encodeURIComponent(boardId)}`, { method: 'PUT', body: JSON.stringify(payload) }),
  deleteBoard: (boardId) => request(`/api/boards/${encodeURIComponent(boardId)}`, { method: 'DELETE' }),
  disconnectBoard: (boardId) => request(`/api/boards/${encodeURIComponent(boardId)}/disconnect`, { method: 'POST' }),
  probeBoard: (boardId) => request(`/api/boards/${encodeURIComponent(boardId)}/probe-target`, { method: 'POST' }),
  probeService: (boardId, publicPort) => request(`/api/boards/${encodeURIComponent(boardId)}/services/${publicPort}/probe-target`, { method: 'POST' })
};
