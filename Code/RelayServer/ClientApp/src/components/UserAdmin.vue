<script setup>
import { onMounted, reactive, ref } from 'vue';
import { api } from '../api';

const users = ref([]);
const error = ref('');
const form = reactive({ username: '', password: '', role: 'User' });
const resetPasswords = reactive({});

async function loadUsers() {
  users.value = await api.users();
}

async function createUser() {
  error.value = '';
  try {
    await api.createUser(form);
    form.username = '';
    form.password = '';
    form.role = 'User';
    await loadUsers();
  } catch (ex) {
    error.value = ex.message || '新增用户失败';
  }
}

async function updateUser(user) {
  error.value = '';
  try {
    await api.updateUser(user.username, { role: user.role });
    await loadUsers();
  } catch (ex) {
    error.value = ex.message || '保存用户失败';
  }
}

async function resetPassword(user) {
  const password = resetPasswords[user.username];
  if (!password) {
    error.value = '请输入新密码';
    return;
  }

  await api.resetPassword(user.username, { newPassword: password });
  resetPasswords[user.username] = '';
}

onMounted(loadUsers);
</script>

<template>
  <div class="panel user-admin">
    <div class="panel-head">
      <div>
        <h2>用户权限</h2>
        <span>新增用户、修改角色、重置密码</span>
      </div>
    </div>

    <form class="user-create simple" @submit.prevent="createUser">
      <input v-model.trim="form.username" placeholder="用户名" required>
      <input v-model="form.password" type="password" minlength="8" placeholder="初始密码" required>
      <select v-model="form.role"><option value="User">普通用户</option><option value="Administrator">管理员</option></select>
      <button class="primary">新增</button>
    </form>

    <p v-if="error" class="error user-error">{{ error }}</p>

    <div class="user-list">
      <div class="user-row user-row-head simple">
        <span>用户名</span>
        <span>角色</span>
        <span>保存</span>
        <span>新密码</span>
        <span>重置</span>
      </div>
      <div v-for="user in users" :key="user.username" class="user-row simple">
        <strong>{{ user.username }}</strong>
        <select v-model="user.role"><option value="User">普通用户</option><option value="Administrator">管理员</option></select>
        <button class="icon" @click="updateUser(user)">保存</button>
        <input v-model="resetPasswords[user.username]" type="password" placeholder="新密码">
        <button class="icon" @click="resetPassword(user)">重置</button>
      </div>
    </div>
  </div>
</template>
