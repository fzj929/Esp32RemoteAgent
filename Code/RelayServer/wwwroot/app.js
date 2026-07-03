const { createApp } = Vue;

createApp({
  data() {
    return {
      authenticated: false,
      username: '',
      boards: [],
      events: [],
      diagnostics: null,
      lastProbe: '',
      error: '',
      loginError: '',
      passwordError: '',
      passwordMessage: '',
      editingExisting: false,
      form: this.emptyForm(),
      loginForm: { username: 'admin', password: '' },
      passwordForm: { currentPassword: '', newPassword: '' },
      timer: null
    };
  },
  computed: {
    onlineCount() {
      return this.boards.filter(x => x.online).length;
    },
    activeConnections() {
      return this.boards.reduce((sum, x) => sum + x.activeConnections, 0);
    },
    enabledCount() {
      return this.boards.filter(x => x.enabled).length;
    },
    totalBytes() {
      return this.boards.reduce((sum, x) => sum + (x.bytesFromPublic || 0) + (x.bytesFromBoard || 0), 0);
    }
  },
  async mounted() {
    await this.checkAuth();
    if (this.authenticated) {
      await this.startPolling();
    }
  },
  beforeUnmount() {
    this.stopPolling();
  },
  methods: {
    emptyForm() {
      return {
        boardId: '',
        name: '',
        authKey: '',
        assignedPort: 6500,
        enabled: true,
        targetHost: '192.168.77.2',
        targetPort: 3389
      };
    },
    async request(path, options = {}) {
      const response = await fetch(path, {
        credentials: 'same-origin',
        ...options,
        headers: {
          ...(options.headers || {})
        }
      });

      if (response.status === 401) {
        this.authenticated = false;
        this.stopPolling();
      }

      return response;
    },
    async checkAuth() {
      const status = await fetch('/api/auth/status', { credentials: 'same-origin' }).then(r => r.json());
      this.authenticated = status.authenticated;
      this.username = status.username || '';
    },
    async startPolling() {
      await this.loadAll();
      this.stopPolling();
      this.timer = setInterval(this.loadAll, 3000);
    },
    stopPolling() {
      if (this.timer) {
        clearInterval(this.timer);
        this.timer = null;
      }
    },
    async login() {
      this.loginError = '';
      const response = await fetch('/api/auth/login', {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(this.loginForm)
      });

      if (response.status === 429) {
        const body = await response.json().catch(() => ({}));
        this.loginError = `登录失败次数过多，请 ${body.retryAfterSeconds || 60} 秒后再试`;
        return;
      }

      if (!response.ok) {
        this.loginError = '用户名或密码错误';
        return;
      }

      await this.checkAuth();
      await this.startPolling();
      this.loginForm.password = '';
    },
    async logout() {
      await this.request('/api/auth/logout', { method: 'POST' });
      this.authenticated = false;
      this.username = '';
      this.boards = [];
      this.events = [];
      this.diagnostics = null;
      this.stopPolling();
    },
    async loadAll() {
      if (!this.authenticated) {
        return;
      }

      const [boardsResponse, eventsResponse, diagnosticsResponse] = await Promise.all([
        this.request('/api/boards'),
        this.request('/api/events'),
        this.request('/api/boards/diagnostics')
      ]);

      if (!boardsResponse.ok || !eventsResponse.ok || !diagnosticsResponse.ok) {
        return;
      }

      this.boards = await boardsResponse.json();
      this.events = await eventsResponse.json();
      this.diagnostics = await diagnosticsResponse.json();
    },
    newBoard() {
      this.error = '';
      this.editingExisting = false;
      this.form = this.emptyForm();
      this.$refs.dialog.showModal();
    },
    editBoard(board) {
      this.error = '';
      this.editingExisting = true;
      this.form = {
        boardId: board.boardId,
        name: board.name,
        authKey: '',
        assignedPort: board.assignedPort,
        enabled: board.enabled,
        targetHost: board.targetHost,
        targetPort: board.targetPort
      };
      this.$refs.dialog.showModal();
    },
    closeDialog() {
      this.$refs.dialog.close();
    },
    async saveBoard() {
      this.error = '';
      const method = this.editingExisting ? 'PUT' : 'POST';
      const path = this.editingExisting ? `/api/boards/${encodeURIComponent(this.form.boardId)}` : '/api/boards';
      const response = await this.request(path, {
        method,
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(this.form)
      });

      if (!response.ok) {
        const body = await response.json().catch(() => ({ error: '保存失败' }));
        this.error = body.error || '保存失败';
        return;
      }

      this.closeDialog();
      await this.loadAll();
    },
    async disconnect(board) {
      await this.request(`/api/boards/${encodeURIComponent(board.boardId)}/disconnect`, { method: 'POST' });
      await this.loadAll();
    },
    async toggleBoardEnabled(board) {
      const nextEnabled = !board.enabled;
      const response = await this.request(`/api/boards/${encodeURIComponent(board.boardId)}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          boardId: board.boardId,
          name: board.name || board.boardId,
          authKey: '',
          assignedPort: board.assignedPort,
          enabled: nextEnabled,
          targetHost: board.targetHost,
          targetPort: board.targetPort
        })
      });

      if (!response.ok) {
        const body = await response.json().catch(() => ({ error: '切换状态失败' }));
        this.lastProbe = body.error || '切换状态失败';
        return;
      }

      this.lastProbe = `${board.boardId} 已${nextEnabled ? '启用' : '禁用'}`;
      await this.loadAll();
    },
    async probeTarget(board) {
      this.lastProbe = '正在测试...';
      const response = await this.request(`/api/boards/${encodeURIComponent(board.boardId)}/probe-target`, { method: 'POST' });
      if (!response.ok) {
        this.lastProbe = '测试失败';
        return;
      }

      const result = await response.json();
      this.lastProbe = result.success
        ? `${result.boardId} ${result.target} 可连接，${result.elapsedMs} ms`
        : `${result.boardId} ${result.target} 不可连接：${result.error}`;
    },
    async removeBoard(board) {
      if (!confirm(`确定删除板子 ${board.boardId}？`)) {
        return;
      }
      await this.request(`/api/boards/${encodeURIComponent(board.boardId)}`, { method: 'DELETE' });
      await this.loadAll();
    },
    openPasswordDialog() {
      this.passwordError = '';
      this.passwordMessage = '';
      this.passwordForm = { currentPassword: '', newPassword: '' };
      this.$refs.passwordDialog.showModal();
    },
    closePasswordDialog() {
      this.$refs.passwordDialog.close();
    },
    async changePassword() {
      this.passwordError = '';
      this.passwordMessage = '';
      const response = await this.request('/api/auth/change-password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(this.passwordForm)
      });

      if (!response.ok) {
        const body = await response.json().catch(() => ({ error: '修改失败' }));
        this.passwordError = body.error || '修改失败';
        return;
      }

      this.passwordMessage = '密码已修改';
      this.passwordForm = { currentPassword: '', newPassword: '' };
    },
    formatTime(value) {
      return new Date(value).toLocaleString();
    },
    formatBytes(value) {
      const number = Number(value || 0);
      if (number < 1024) {
        return `${number} B`;
      }
      if (number < 1024 * 1024) {
        return `${(number / 1024).toFixed(1)} KB`;
      }
      if (number < 1024 * 1024 * 1024) {
        return `${(number / 1024 / 1024).toFixed(1)} MB`;
      }
      return `${(number / 1024 / 1024 / 1024).toFixed(1)} GB`;
    }
  }
}).mount('#app');
