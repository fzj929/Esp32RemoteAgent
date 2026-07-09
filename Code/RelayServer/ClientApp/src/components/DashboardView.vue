<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { api } from '../api';
import BoardDialog from './BoardDialog.vue';
import PasswordDialog from './PasswordDialog.vue';
import PortAdmin from './PortAdmin.vue';
import UserAdmin from './UserAdmin.vue';

const props = defineProps({ session: { type: Object, required: true } });
const emit = defineEmits(['logout', 'session-change']);

const activeView = ref('boards');
const boards = ref([]);
const diagnostics = ref(null);
const events = ref([]);
const users = ref([]);
const publicPorts = ref([]);
const lastProbe = ref({ boardId: '', target: '', detail: '暂无', tone: 'muted' });
const boardDialog = ref(null);
const passwordDialog = ref(null);
let timer = null;

const isAdmin = computed(() => props.session.role === 'Administrator');
const onlineCount = computed(() => boards.value.filter(x => x.online).length);
const activeConnections = computed(() => boards.value.reduce((sum, x) => sum + x.activeConnections, 0));
const enabledServices = computed(() => boards.value.reduce((sum, x) => sum + (x.enabled ? servicesOf(x).filter(s => s.enabled).length : 0), 0));
const totalBytes = computed(() => boards.value.reduce((sum, x) => sum + (x.bytesFromPublic || 0) + (x.bytesFromBoard || 0), 0));

function servicesOf(board) {
  return Array.isArray(board.services) && board.services.length > 0
    ? board.services
    : [{ name: 'RDP', publicPort: board.assignedPort, targetHost: board.targetHost, targetPort: board.targetPort, enabled: true }];
}

function serviceSummary(board) {
  return servicesOf(board)
    .map(x => `${x.name || 'TCP'} ${x.publicPort} -> ${x.targetHost}:${x.targetPort}${x.enabled ? '' : '（停用）'}`)
    .join('；');
}

function ownerText(board) {
  return board.ownerUsername || '仅管理员';
}

async function loadAll() {
  const jobs = [api.boards(), api.diagnostics(), api.publicPorts().catch(() => [])];
  if (isAdmin.value) {
    jobs.push(api.events().catch(() => []), api.users().catch(() => []));
  }

  const result = await Promise.all(jobs);
  boards.value = result[0] || [];
  diagnostics.value = result[1] || null;
  publicPorts.value = result[2] || [];
  events.value = result[3] || [];
  users.value = result[4] || [];
}

function startPolling() {
  stopPolling();
  timer = setInterval(loadAll, 3000);
}

function stopPolling() {
  if (timer) {
    clearInterval(timer);
    timer = null;
  }
}

function setView(view) {
  activeView.value = view;
}

function editBoard(board = null) {
  boardDialog.value.open(board, users.value, publicPorts.value);
}

async function saveBoard(payload) {
  if (payload.editingExisting) {
    await api.updateBoard(payload.board.boardId, payload.board);
  } else {
    await api.createBoard(payload.board);
  }
  await loadAll();
}

async function toggleBoard(board) {
  const services = servicesOf(board);
  const next = { ...board, enabled: !board.enabled, services };
  await api.updateBoard(board.boardId, next);
  await loadAll();
}

async function disconnect(board) {
  await api.disconnectBoard(board.boardId);
  await loadAll();
}

async function removeBoard(board) {
  if (!confirm(`确定删除板子 ${board.boardId}？`)) {
    return;
  }

  await api.deleteBoard(board.boardId);
  await loadAll();
}

async function probe(board, service = null) {
  const selected = service || servicesOf(board)[0];
  lastProbe.value = {
    boardId: board.boardId,
    target: `${selected.targetHost}:${selected.targetPort}`,
    detail: '正在测试...',
    tone: 'muted'
  };

  const result = service
    ? await api.probeService(board.boardId, selected.publicPort)
    : await api.probeBoard(board.boardId);

  lastProbe.value = {
    boardId: result.boardId,
    target: result.target,
    detail: result.success ? `可连接，${result.elapsedMs} ms` : `不可连接：${result.error}`,
    tone: result.success ? 'ok' : 'bad'
  };
}

function formatBytes(value) {
  const number = Number(value || 0);
  if (number < 1024) return `${number} B`;
  if (number < 1024 * 1024) return `${(number / 1024).toFixed(1)} KB`;
  if (number < 1024 * 1024 * 1024) return `${(number / 1024 / 1024).toFixed(1)} MB`;
  return `${(number / 1024 / 1024 / 1024).toFixed(1)} GB`;
}

function formatTime(value) {
  return value ? new Date(value).toLocaleString() : '-';
}

onMounted(async () => {
  await loadAll();
  startPolling();
});
onUnmounted(stopPolling);
</script>

<template>
  <div class="app-shell">
    <aside class="rail">
      <div class="brand">
        <div class="mark">R</div>
        <strong>Relay Desk</strong>
        <span>ESP32-S3</span>
      </div>
    </aside>

    <main>
      <section class="topbar">
        <div>
          <p class="kicker">TCP tunnel control</p>
          <h1>远程 TCP 中转后台</h1>
        </div>
        <div class="actions">
          <span class="user-pill">{{ session.username }} · {{ isAdmin ? '管理员' : '普通用户' }}</span>
          <button class="ghost" @click="passwordDialog.open()">修改密码</button>
          <button class="ghost" @click="loadAll">刷新</button>
          <button v-if="activeView === 'boards'" class="primary" @click="editBoard()">新增板子</button>
          <button class="ghost" @click="emit('logout')">退出</button>
        </div>
      </section>

      <nav class="nav-tabs" aria-label="后台菜单">
        <button :class="{ active: activeView === 'boards' }" @click="setView('boards')">板子管理</button>
        <button v-if="isAdmin" :class="{ active: activeView === 'ports' }" @click="setView('ports')">公网端口</button>
        <button v-if="isAdmin" :class="{ active: activeView === 'users' }" @click="setView('users')">用户权限</button>
      </nav>

      <template v-if="activeView === 'boards'">
        <section class="stats">
          <article><span>在线板子</span><strong>{{ onlineCount }}</strong></article>
          <article><span>活动连接</span><strong>{{ activeConnections }}</strong></article>
          <article><span>公网流量</span><strong>{{ formatBytes(totalBytes) }}</strong></article>
          <article><span>启用服务</span><strong>{{ enabledServices }}</strong></article>
        </section>

        <section class="content-grid">
          <div class="panel devices">
            <div class="panel-head">
              <div>
                <h2>板子列表</h2>
                <span>{{ boards.length }} 台设备</span>
              </div>
            </div>
            <div class="table">
              <div class="row header">
                <span>状态</span><span>板子</span><span>TCP 服务</span><span>连接</span><span>诊断</span><span>操作</span>
              </div>
              <div v-if="boards.length === 0" class="row empty"><span>暂无板子。</span></div>
              <div v-for="board in boards" :key="board.boardId" class="row">
                <span class="status">
                  <i :class="['dot', board.online ? 'on' : 'off', !board.enabled ? 'disabled' : '']"></i>
                  {{ board.enabled ? (board.online ? '在线' : '离线') : '禁用' }}
                </span>
                <span>
                  <strong>{{ board.name || board.boardId }}</strong>
                  <small>{{ board.boardId }} · {{ ownerText(board) }} · {{ board.firmware || '未上报版本' }}</small>
                </span>
                <span class="services-cell">
                  <button v-for="service in servicesOf(board)" :key="service.publicPort" class="service-chip" :class="{ disabled: !service.enabled }" @click="probe(board, service)">
                    <b>{{ service.name || 'TCP' }}</b><em>{{ service.publicPort }}</em>
                  </button>
                  <small class="service-summary">{{ serviceSummary(board) }}</small>
                </span>
                <span>{{ board.activeConnections }}</span>
                <span>
                  <small>RSSI {{ board.telemetry?.rssi ?? '-' }}</small>
                  <small>Heap {{ formatBytes(board.telemetry?.freeHeap || 0) }}</small>
                  <small>心跳 {{ formatTime(board.lastHeartbeat) }}</small>
                </span>
                <span class="row-actions">
                  <button class="icon toggle" @click="toggleBoard(board)">{{ board.enabled ? '禁用' : '启用' }}</button>
                  <button class="icon" @click="editBoard(board)">编辑</button>
                  <button class="icon" @click="probe(board)">测试</button>
                  <button class="icon" :disabled="!board.online" @click="disconnect(board)">断开</button>
                  <button class="icon danger" @click="removeBoard(board)">删除</button>
                </span>
              </div>
            </div>
          </div>

          <div class="side-stack">
            <div class="panel side">
              <div class="panel-head">
                <div><h2>诊断</h2><span>服务器时间 {{ formatTime(diagnostics?.serverTime) }}</span></div>
              </div>
              <div class="diagnostics">
                <p><span>上行到板子</span><strong>{{ formatBytes(diagnostics?.bytesFromPublic || 0) }}</strong></p>
                <p><span>板子返回</span><strong>{{ formatBytes(diagnostics?.bytesFromBoard || 0) }}</strong></p>
                <p class="probe-row">
                  <span>最近测试</span>
                  <strong :class="['probe-result', lastProbe.tone]">
                    <em v-if="lastProbe.boardId">{{ lastProbe.boardId }} · {{ lastProbe.target }}</em>
                    <b>{{ lastProbe.detail }}</b>
                  </strong>
                </p>
              </div>
            </div>
            <div v-if="isAdmin" class="panel side">
              <div class="panel-head"><div><h2>事件日志</h2><span>最近 200 条</span></div></div>
              <ol class="events">
                <li v-if="events.length === 0">暂无事件。</li>
                <li v-for="event in events" :key="event.timestamp + event.message"><time>{{ formatTime(event.timestamp) }}</time><p>{{ event.message }}</p></li>
              </ol>
            </div>
          </div>
        </section>
      </template>

      <section v-else-if="activeView === 'ports' && isAdmin" class="user-admin-page">
        <PortAdmin />
      </section>

      <section v-else-if="activeView === 'users' && isAdmin" class="user-admin-page">
        <UserAdmin />
      </section>
    </main>

    <BoardDialog ref="boardDialog" :is-admin="isAdmin" :session="session" :save-handler="saveBoard" />
    <PasswordDialog ref="passwordDialog" @changed="emit('session-change')" />
  </div>
</template>
