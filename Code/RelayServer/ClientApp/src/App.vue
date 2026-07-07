<script setup>
import { computed, onMounted, ref } from 'vue';
import { api } from './api';
import LoginView from './components/LoginView.vue';
import DashboardView from './components/DashboardView.vue';

const session = ref({ authenticated: false, username: '', role: '' });
const loading = ref(true);

const isAuthenticated = computed(() => session.value.authenticated);

async function refreshSession() {
  loading.value = true;
  try {
    session.value = await api.status();
  } finally {
    loading.value = false;
  }
}

async function handleLogin() {
  await refreshSession();
}

async function handleLogout() {
  await api.logout();
  session.value = { authenticated: false, username: '', role: '' };
}

onMounted(refreshSession);
</script>

<template>
  <div v-if="loading" class="boot-screen">加载中</div>
  <LoginView v-else-if="!isAuthenticated" @login="handleLogin" />
  <DashboardView v-else :session="session" @logout="handleLogout" @session-change="refreshSession" />
</template>
