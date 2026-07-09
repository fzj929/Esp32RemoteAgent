<script setup>
import { onMounted, reactive, ref } from 'vue';
import { api } from '../api';

const ports = ref([]);
const error = ref('');
const form = reactive({ publicPort: 6500, customerName: '', note: '', enabled: true });

async function load() {
  ports.value = await api.publicPorts();
}

function edit(port) {
  form.publicPort = port.publicPort;
  form.customerName = port.customerName;
  form.note = port.note || '';
  form.enabled = port.enabled;
}

function reset() {
  form.publicPort = 6500;
  form.customerName = '';
  form.note = '';
  form.enabled = true;
}

async function save() {
  error.value = '';
  try {
    await api.createPublicPort({
      publicPort: Number(form.publicPort),
      customerName: form.customerName,
      note: form.note,
      enabled: Boolean(form.enabled)
    });
    reset();
    await load();
  } catch (ex) {
    error.value = ex.message || '保存失败';
  }
}

async function remove(port) {
  if (!confirm(`确定删除公网端口 ${port.publicPort}？`)) {
    return;
  }

  error.value = '';
  try {
    await api.deletePublicPort(port.publicPort);
    await load();
  } catch (ex) {
    error.value = ex.message || '删除失败';
  }
}

function usageText(port) {
  return port.usedByBoardId ? `${port.usedByBoardId} / ${port.usedByServiceName || 'TCP'}` : '未占用';
}

onMounted(load);
</script>

<template>
  <div class="panel port-admin">
    <div class="panel-head">
      <div>
        <h2>公网端口客户配置</h2>
        <span>每个公网端口只能分配给一个客户，已被板子占用的端口不能重复使用</span>
      </div>
    </div>

    <form class="port-create" @submit.prevent="save">
      <input v-model.number="form.publicPort" type="number" min="6500" max="6600" placeholder="公网端口" required>
      <input v-model.trim="form.customerName" placeholder="客户名称" required>
      <input v-model.trim="form.note" placeholder="备注">
      <select v-model="form.enabled">
        <option :value="true">启用</option>
        <option :value="false">停用</option>
      </select>
      <button class="primary">保存</button>
      <button class="ghost" type="button" @click="reset">清空</button>
    </form>

    <p v-if="error" class="error port-error">{{ error }}</p>

    <div class="port-list">
      <div class="port-row port-row-head">
        <span>端口</span>
        <span>客户</span>
        <span>状态</span>
        <span>占用</span>
        <span>备注</span>
        <span>操作</span>
      </div>
      <div v-if="ports.length === 0" class="port-row empty"><span>暂无公网端口配置。</span></div>
      <div v-for="port in ports" :key="port.publicPort" class="port-row">
        <strong>{{ port.publicPort }}</strong>
        <span>{{ port.customerName }}</span>
        <span><i :class="['dot', port.enabled ? 'on' : 'disabled']"></i>{{ port.enabled ? '启用' : '停用' }}</span>
        <span>{{ usageText(port) }}</span>
        <small>{{ port.note || '-' }}</small>
        <span class="row-actions">
          <button class="icon" @click="edit(port)">编辑</button>
          <button class="icon danger" :disabled="Boolean(port.usedByBoardId)" @click="remove(port)">删除</button>
        </span>
      </div>
    </div>
  </div>
</template>
