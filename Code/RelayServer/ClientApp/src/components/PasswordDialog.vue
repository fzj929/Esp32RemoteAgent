<script setup>
import { reactive, ref } from 'vue';
import { api } from '../api';

const emit = defineEmits(['changed']);
const visible = ref(false);
const error = ref('');
const message = ref('');
const form = reactive({ currentPassword: '', newPassword: '' });

function open() {
  error.value = '';
  message.value = '';
  form.currentPassword = '';
  form.newPassword = '';
  visible.value = true;
}

function close() {
  visible.value = false;
}

async function save() {
  error.value = '';
  message.value = '';
  try {
    await api.changePassword(form);
    message.value = '密码已修改';
    emit('changed');
  } catch (ex) {
    error.value = ex.message || '修改失败';
  }
}

defineExpose({ open });
</script>

<template>
  <dialog :open="visible" class="editor">
    <form method="dialog" @submit.prevent="save">
      <div class="dialog-head">
        <h2>修改密码</h2>
        <button class="close" type="button" @click="close">×</button>
      </div>
      <label>当前密码<input v-model="form.currentPassword" type="password" required></label>
      <label>新密码<input v-model="form.newPassword" type="password" minlength="8" required></label>
      <p v-if="error" class="error">{{ error }}</p>
      <p v-if="message" class="success">{{ message }}</p>
      <div class="dialog-actions">
        <button class="ghost dark" type="button" @click="close">取消</button>
        <button class="primary" type="submit">保存</button>
      </div>
    </form>
  </dialog>
</template>
