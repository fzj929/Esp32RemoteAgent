<script setup>
import { reactive, ref } from 'vue';
import { api } from '../api';

const emit = defineEmits(['login']);
const form = reactive({ username: 'admin', password: '' });
const error = ref('');
const busy = ref(false);

async function submit() {
  error.value = '';
  busy.value = true;
  try {
    await api.login(form);
    form.password = '';
    emit('login');
  } catch (ex) {
    error.value = ex.status === 429 ? '登录失败次数过多，请稍后再试' : '用户名或密码错误';
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <main class="login-shell">
    <form class="login-card" @submit.prevent="submit">
      <div class="brand-row">
        <div class="mark">R</div>
        <div>
          <strong>Relay Desk</strong>
          <span>ESP32-S3 TCP remote access</span>
        </div>
      </div>
      <h1>管理员登录</h1>
      <label>用户名<input v-model.trim="form.username" autocomplete="username" required></label>
      <label>密码<input v-model="form.password" type="password" autocomplete="current-password" required></label>
      <p v-if="error" class="error">{{ error }}</p>
      <button class="primary wide" :disabled="busy">{{ busy ? '登录中' : '登录' }}</button>
      <p class="hint">首次启动默认账号 admin / admin123456，登录后请立即修改密码。</p>
    </form>
  </main>
</template>
