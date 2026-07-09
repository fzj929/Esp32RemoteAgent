function formatBytes(value) {
  var n = Number(value || 0);
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + ' MB';
  return (n / 1024 / 1024 / 1024).toFixed(1) + ' GB';
}

function setMessage(text, kind) {
  $('#message').text(text || '').removeClass('ok error').addClass(kind || '');
}

function loadConfig() {
  return $.getJSON('/api/config').done(function (cfg) {
    $('#enabled').val(String(Boolean(cfg.enabled)));
    $('#boardId').val(cfg.boardId);
    $('#boardKey').val(cfg.boardKey);
    $('#relayHost').val(cfg.relayHost);
    $('#relayPort').val(cfg.relayPort);
    $('#defaultTargetHost').val(cfg.defaultTargetHost);
    $('#defaultTargetPort').val(cfg.defaultTargetPort);
  });
}

function loadStatus() {
  return $.getJSON('/api/status').done(function (s) {
    $('#statusTime').text(new Date().toLocaleString());
    $('#online').text(s.online ? '在线' : (s.enabled ? '离线' : '停用'))
      .removeClass('state-on state-off')
      .addClass(s.online ? 'state-on' : 'state-off');
    $('#relay').text(s.relay);
    $('#activeTunnels').text(s.activeTunnels);
    $('#bytesFromServer').text(formatBytes(s.bytesFromServer));
    $('#bytesFromTarget').text(formatBytes(s.bytesFromTarget));
    $('#lastHeartbeat').text(s.lastHeartbeat ? new Date(s.lastHeartbeat).toLocaleString() : '-');
    $('#lastError').text(s.lastError || '-');
  });
}

function saveConfig() {
  setMessage('', '');
  var payload = {
    enabled: $('#enabled').val() === 'true',
    boardId: $('#boardId').val(),
    boardKey: $('#boardKey').val(),
    relayHost: $('#relayHost').val(),
    relayPort: Number($('#relayPort').val()),
    defaultTargetHost: $('#defaultTargetHost').val(),
    defaultTargetPort: Number($('#defaultTargetPort').val())
  };

  $.ajax({
    url: '/api/config',
    method: 'PUT',
    contentType: 'application/json',
    data: JSON.stringify(payload)
  }).done(function () {
    setMessage('配置已保存，代理将在下一轮重连时使用新配置。', 'ok');
    loadStatus();
  }).fail(function (xhr) {
    var body = xhr.responseJSON || {};
    setMessage(body.error || '保存失败', 'error');
  });
}

$(function () {
  $('#configForm').on('submit', function (event) {
    event.preventDefault();
    saveConfig();
  });
  $('#refresh').on('click', function () {
    loadConfig();
    loadStatus();
  });

  loadConfig();
  loadStatus();
  setInterval(loadStatus, 3000);
});
