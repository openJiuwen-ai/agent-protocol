-- 一体机 Agent OS 注册中心 · 表结构（构建预制 .db 用）
-- 真源在 a2x_registry/common/db.py 的 SCHEMA_SQL；本文件由 build_fixtures.sh 自动生成。
--    请勿直接编辑本文件；改 schema 请改 a2x_registry/common/db.py。

-- Agent OS registry table schema (authoritative)
-- Use CREATE TABLE/INDEX IF NOT EXISTS so startup-time creation is idempotent.

-- Registry meta: which named registries exist, their kind, and config
CREATE TABLE IF NOT EXISTS registry_meta (
  registry TEXT PRIMARY KEY,             -- 'toolret'/'publicmcp'/'default'/'image-registry'/'instance-registry'
  kind     TEXT NOT NULL,                -- service | image | instance
  config   TEXT                          -- JSON: service-kind stores register_config/vector_config/taxonomy_hash
);

-- Service (A2X: generic/a2a/skill) -- discovery / classification rely on it
CREATE TABLE IF NOT EXISTS service (
  registry    TEXT NOT NULL,
  service_id  TEXT NOT NULL,
  type        TEXT NOT NULL,             -- generic | a2a | skill
  source      TEXT NOT NULL,             -- user_config | api_config | ephemeral | skill_folder
  name        TEXT,                      -- hot: classification LLM input / filter
  description TEXT,                      -- hot: classification LLM input
  data        TEXT NOT NULL,             -- JSON: service_data / agent_card / skill_data
  created_at  TEXT NOT NULL,
  updated_at  TEXT NOT NULL,
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_service_type ON service(registry, type);

-- Image (one row per version)
CREATE TABLE IF NOT EXISTS image (
  registry          TEXT NOT NULL,
  service_id        TEXT NOT NULL,               -- image_sid(framework, framework_version)
  framework         TEXT NOT NULL,               -- hot: lookup by framework
  framework_version TEXT NOT NULL,               -- hot: lookup by version
  version_key       TEXT NOT NULL,               -- sort: normalized semver key computed at registration (see image/version_key.py)
  is_default        INTEGER NOT NULL DEFAULT 0,  -- default-version flag for a framework (exactly one row per framework = 1); not part of sort order
  uploaded_by       TEXT,                        -- hot: filter by uploader; pre-seeded entries are 'system'
  data              TEXT NOT NULL,               -- JSON flat (no rootfs wrapper): {imageurl, workdir, mounts, cpu, memory, ports, env, image_module_version, created_at}
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_image_fw     ON image(registry, framework);
CREATE INDEX IF NOT EXISTS idx_image_fw_ver ON image(registry, framework, framework_version);
CREATE INDEX IF NOT EXISTS idx_image_by     ON image(registry, uploaded_by);
CREATE INDEX IF NOT EXISTS idx_image_order  ON image(registry, framework, version_key DESC);

-- Instance (status is not persisted; derived from node heartbeat at query time)
CREATE TABLE IF NOT EXISTS instance (
  registry          TEXT NOT NULL,
  service_id        TEXT NOT NULL,       -- instance_sid(user, framework)
  kind              TEXT NOT NULL,       -- third-party | jiuwen
  framework         TEXT,
  framework_version TEXT,
  node              TEXT,                -- hot: bulk eviction by node / lookup by node
  "user"            TEXT,                -- hot: lookup a user's instances by user id
  data              TEXT NOT NULL,       -- JSON {address, created_at, last_active_at}
  PRIMARY KEY (registry, service_id)
);
CREATE INDEX IF NOT EXISTS idx_instance_node ON instance(registry, node);
CREATE INDEX IF NOT EXISTS idx_instance_fw   ON instance(registry, framework, framework_version);
CREATE INDEX IF NOT EXISTS idx_instance_user ON instance(registry, "user");
CREATE INDEX IF NOT EXISTS idx_instance_order ON instance(registry, framework, "user", service_id);
