"""``RegistryService.list_datasets`` 只应返回真正的数据集目录。

``<database_dir>`` 下与数据集同级的还有共享 ChromaDB 目录（见
``chroma_dir()``）。早期实现朴素地遍历该目录下所有子目录，会把 ``chroma``
当成一个数据集返回，进而影响：

  * CLI ``register --list`` 的输出；
  * 集群命名空间协商（``cluster/auth_handshake.py`` 用
    ``ns in set(registry_svc.list_datasets())`` 判定"本节点已托管"，
    ``chroma`` 因此绕过新建命名空间的 admin 审批）；
  * ``dataset in registry.list_datasets()`` 这类存在性守卫。

同时该函数曾被重复定义两次，后者静默覆盖前者，前者是不可达死代码。
"""

from __future__ import annotations

from pathlib import Path

from a2x_registry.register.service import RegistryService


def _mk_dataset(root: Path, name: str) -> Path:
    """建一个带数据集标记文件（register_config.json）的目录。"""
    dataset_dir = root / name
    dataset_dir.mkdir(parents=True)
    (dataset_dir / "register_config.json").write_text("{}", encoding="utf-8")
    return dataset_dir


def test_list_datasets_excludes_shared_chroma_dir(tmp_path):
    root = tmp_path / "database"
    root.mkdir()
    service = RegistryService(root)

    _mk_dataset(root, "ds_alpha")
    _mk_dataset(root, "ds_beta")
    # 共享向量库目录，与数据集同级
    service.chroma_dir().mkdir()
    # 没有数据集标记的空目录
    (root / "not_a_dataset").mkdir()

    assert service.list_datasets() == ["ds_alpha", "ds_beta"]


def test_list_datasets_empty_when_database_dir_missing(tmp_path):
    service = RegistryService(tmp_path / "database_absent")

    assert service.list_datasets() == []


def test_list_datasets_single_definition():
    """同名方法必须只定义一次，避免后定义静默覆盖前定义。"""
    import ast
    import inspect
    import textwrap

    tree = ast.parse(textwrap.dedent(inspect.getsource(RegistryService)))
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef))
    defs = [
        n for n in cls.body
        if isinstance(n, ast.FunctionDef) and n.name == "list_datasets"
    ]
    assert len(defs) == 1, "list_datasets 被重复定义 %d 次" % len(defs)

    source = inspect.getsource(RegistryService.list_datasets)
    # 不应退回到"朴素遍历 <database_dir> 所有子目录"
    assert "iterdir" not in source
