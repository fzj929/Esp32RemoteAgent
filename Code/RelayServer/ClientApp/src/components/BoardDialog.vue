<script setup>
import { computed, reactive, ref } from 'vue';

const props = defineProps({
  isAdmin: { type: Boolean, required: true },
  session: { type: Object, required: true }
});
const emit = defineEmits(['save']);

const visible = ref(false);
const editingExisting = ref(false);
const users = ref([]);
const error = ref('');
const form = reactive(emptyBoard());

const assignableUsers = computed(() => users.value.filter(x => x.role !== 'Administrator'));

function emptyBoard() {
  return {
    boardId: '',
    name: '',
    authKey: '',
    assignedPort: 6500,
    enabled: true,
    ownerUsername: '',
    targetHost: '192.168.77.2',
    targetPort: 3389,
    services: [
      { name: 'RDP', publicPort: 6500, targetHost: '192.168.77.2', targetPort: 3389, enabled: true }
    ]
  };
}

function servicesOf(board) {
  return Array.isArray(board?.services) && board.services.length > 0
    ? board.services
    : [{ name: 'RDP', publicPort: board.assignedPort, targetHost: board.targetHost, targetPort: board.targetPort, enabled: true }];
}

function assign(target, source) {
  Object.keys(target).forEach(key => delete target[key]);
  Object.assign(target, source);
}

function open(board = null, userList = []) {
  error.value = '';
  users.value = userList || [];
  editingExisting.value = Boolean(board);
  if (board) {
    const services = servicesOf(board).map(x => ({ ...x }));
    assign(form, {
      boardId: board.boardId,
      name: board.name,
      authKey: '',
      assignedPort: services[0].publicPort,
      enabled: board.enabled,
      ownerUsername: board.ownerUsername || '',
      targetHost: services[0].targetHost,
      targetPort: services[0].targetPort,
      services
    });
  } else {
    const next = emptyBoard();
    next.ownerUsername = props.isAdmin ? '' : props.session.username;
    assign(form, next);
  }
  visible.value = true;
}

function close() {
  visible.value = false;
}

function addService() {
  const used = new Set(form.services.map(x => Number(x.publicPort)));
  let port = 6500;
  while (used.has(port) && port < 6600) port += 1;
  form.services.push({ name: 'HTTP', publicPort: port, targetHost: '192.168.77.2', targetPort: 80, enabled: true });
}

function removeService(index) {
  if (form.services.length <= 1) {
    error.value = '至少保留一条 TCP 服务';
    return;
  }
  form.services.splice(index, 1);
}

async function save() {
  error.value = '';
  const primary = form.services[0];
  const board = {
    ...form,
    ownerUsername: props.isAdmin ? (form.ownerUsername || null) : props.session.username,
    assignedPort: Number(primary.publicPort),
    targetHost: primary.targetHost,
    targetPort: Number(primary.targetPort),
    services: form.services.map(x => ({
      name: x.name,
      publicPort: Number(x.publicPort),
      targetHost: x.targetHost,
      targetPort: Number(x.targetPort),
      enabled: Boolean(x.enabled)
    }))
  };
  try {
    await emit('save', { editingExisting: editingExisting.value, board });
    close();
  } catch (ex) {
    error.value = ex.message || '保存失败';
  }
}

defineExpose({ open });
</script>

<template>
  <dialog :open="visible" class="editor wide-editor">
    <form method="dialog" @submit.prevent="save">
      <div class="dialog-head">
        <h2>{{ editingExisting ? '编辑板子' : '新增板子' }}</h2>
        <button class="close" type="button" @click="close">×</button>
      </div>

      <label>板子 ID<input v-model.trim="form.boardId" :disabled="editingExisting" required></label>
      <label>显示名称<input v-model.trim="form.name" placeholder="例如：一号产线终端"></label>
      <label>认证密钥<input v-model.trim="form.authKey" :required="!editingExisting" :placeholder="editingExisting ? '留空表示不修改' : ''"></label>
      <div class="split">
        <label>启用状态<select v-model="form.enabled"><option :value="true">启用</option><option :value="false">停用</option></select></label>
        <label v-if="isAdmin">归属用户
          <select v-model="form.ownerUsername">
            <option value="">仅管理员可见</option>
            <option v-for="user in assignableUsers" :key="user.username" :value="user.username">{{ user.username }}</option>
          </select>
        </label>
      </div>

      <section class="service-editor">
        <div class="service-editor-head">
          <h3>TCP 服务</h3>
          <button class="ghost dark" type="button" @click="addService">新增服务</button>
        </div>
        <div v-for="(service, index) in form.services" :key="index" class="service-form-row">
          <label>名称<input v-model.trim="service.name" required></label>
          <label>公网端口<input v-model.number="service.publicPort" type="number" min="6500" max="6600" required></label>
          <label>终端 IP<input v-model.trim="service.targetHost" required></label>
          <label>终端端口<input v-model.number="service.targetPort" type="number" min="1" max="65535" required></label>
          <label>状态<select v-model="service.enabled"><option :value="true">启用</option><option :value="false">停用</option></select></label>
          <button class="icon danger" type="button" @click="removeService(index)">删除</button>
        </div>
      </section>

      <p v-if="error" class="error">{{ error }}</p>
      <div class="dialog-actions">
        <button class="ghost dark" type="button" @click="close">取消</button>
        <button class="primary" type="submit">保存</button>
      </div>
    </form>
  </dialog>
</template>
