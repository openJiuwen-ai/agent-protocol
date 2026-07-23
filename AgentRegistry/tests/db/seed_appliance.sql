-- 一体机 appliance 模式样例数据
-- 覆盖：3 个命名注册表（service/image/instance 各一）+ 多框架多版本镜像 + 多节点多用户实例
-- 供 tests/db/test_schema.py / test_image_sql.py / test_instance_sql.py 只读校验。

-- ── registry_meta ───────────────────────────────────────────
INSERT INTO registry_meta(registry, kind, config) VALUES
  ('default',     'service',  '{"taxonomy_hash":"sha1:deadbeef","register_config":{"v":1}}'),
  ('images',  'image',    NULL),
  ('instances',  'instance', NULL);

-- ── service（A2X 向前兼容：default 注册表一行 generic） ──────
INSERT INTO service(registry, service_id, type, source, name, description, data, created_at, updated_at) VALUES
  ('default', 'svc_demo1', 'generic', 'api_config', 'demo-service', 'a demo A2X service',
   '{"endpoint":"http://127.0.0.1:9000/demo"}', '2026-07-01T00:00:00Z', '2026-07-01T00:00:00Z');

-- ── image（镜像注册表：langchain 两版 0.2.0 默认；llama_index 一版默认） ──
-- data JSON 含 runtime_spec（不透明透传）+ env_vars/workspace/mounts 顶层字段
INSERT INTO image(registry, service_id, framework, framework_version, version_key, is_default, uploaded_by, data) VALUES
  ('images', 'img_langchain_0.1.0',    'langchain',   '0.1.0',  '00000.00001.00000~', 0, 'system',
   '{"runtime_spec":{"runtime":"python3.11","sandbox_type":"docker","rootfs":{"imageurl":"registry.local/langchain:0.1.0","user":"agentos","ports":["tcp:8080"]},"cpu":1,"memory":"512Mi","ports":[8080]},"env_vars":{"LC_CACHE":"/tmp/lc"},"workspace":"/app","mounts":[],"image_module_version":"v1","created_at":"2026-07-01T00:00:00Z"}'),
  ('images', 'img_langchain_0.2.0',    'langchain',   '0.2.0',  '00000.00002.00000~', 1, 'system',
   '{"runtime_spec":{"runtime":"python3.11","sandbox_type":"docker","rootfs":{"imageurl":"registry.local/langchain:0.2.0","user":"agentos","ports":["tcp:8080"]},"cpu":2,"memory":"1Gi","ports":[8080]},"env_vars":{"LC_CACHE":"/tmp/lc2"},"workspace":"/app","mounts":[],"image_module_version":"v1","created_at":"2026-07-02T00:00:00Z"}'),
  ('images', 'img_llama_index_0.9.0',  'llama_index', '0.9.0',  '00000.00009.00000~', 1, 'system',
   '{"runtime_spec":{"runtime":"python3.11","sandbox_type":"docker","rootfs":{"imageurl":"registry.local/llama_index:0.9.0","user":"agentos","ports":["tcp:9000"]},"cpu":2,"memory":"2Gi","ports":[9000]},"env_vars":{"LI_CACHE":"/tmp/li"},"workspace":"/app","mounts":[],"image_module_version":"v1","created_at":"2026-07-03T00:00:00Z"}');

-- ── instance（实例注册表：3 实例，2 节点，2 用户） ───────────
-- service_id = instance_sid(user, framework) = "generic_" + sha256(user|framework)[:8]
-- 此处手算占位（实现时由 common/ids.py 计算），保证 (user, framework) 唯一
INSERT INTO instance(registry, service_id, kind, framework, framework_version, node, "user", data) VALUES
  ('instances', 'generic_a1b2c3d4', '三方', 'langchain',   '0.2.0', '192.168.0.11', 'alice',
   '{"address":"http://192.168.0.11:18080","created_at":"2026-07-10T08:00:00Z","last_active_at":"2026-07-13T09:00:00Z"}'),
  ('instances', 'generic_e5f67890', '三方', 'llama_index', '0.9.0', '192.168.0.11', 'alice',
   '{"address":"http://192.168.0.11:19000","created_at":"2026-07-11T08:00:00Z","last_active_at":"2026-07-13T09:05:00Z"}'),
  ('instances', 'generic_11223344', '九问', 'langchain',   '0.1.0', '192.168.0.12', 'bob',
   '{"address":"http://192.168.0.12:18080","created_at":"2026-07-12T08:00:00Z","last_active_at":"2026-07-13T09:10:00Z"}');
