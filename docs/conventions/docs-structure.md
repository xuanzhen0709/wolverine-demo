> [📖 Docs](../README.md) › Conventions › 文档结构标准

# 文档结构标准（docs-structure）

文档组织约定。

## 标准骨架

```
README.md                  # 仓库入口：一段话定位 + 指向 docs/ 的链接（不堆内容）
docs/
├── README.md              # 文档索引 / 目录（“地图”）
├── overview/              # 理解导向：架构、端到端流程、设计背景
├── guides/                # 操作导向：how-to，按任务组织（“如何部署一个策略”）
├── reference/             # 信息导向：规格与查阅（playbook 列表、schema、CLI）
├── conventions/           # 跨子系统的编写规范
└── roadmap.md             # 待办 / 未来工作
```

四个分区借鉴 [Diátaxis](https://diataxis.fr/) 的核心区分，但简化为团队最常用的四类：

| 分区 | 回答的问题 | 写什么 |
| --- | --- | --- |
| `overview/` | “这是什么？怎么运作？” | 架构图、数据流、设计决策的背景 |
| `guides/` | “我怎么做 X？” | 面向任务的步骤、命令示例 |
| `reference/` | “X 的确切定义/参数是什么？” | 表格、规格、API/CLI、配置项清单 |
| `conventions/` | “我该怎么写代码/文档？” | 编码与文档规范，跨子系统通用 |

## 配套规则

1. **每个目录的长期文档是 `README.md`**（编辑器与 Git 托管会自动展示）；深入主题用
   带语义的文件名（如 `inventory-and-targets.md`、`docs-structure.md`）。
2. **根 `README.md` 是地图**：一段话定位 + 顶层目录表 + 指向 `docs/` 的链接，不复制内容。
3. **代码目录放 stub**：每个被文档化的代码目录保留一个 3–4 行的 `README.md`，指向
   `docs/` 中对应页面 —— 这样「打开目录就知道它是什么」，而正文只有一份。
4. **临时/工作笔记不进文档树**：feature spec、临时 TODO 等放进仓库根的 `notes/`
   （加入 `.gitignore`），不要与长期参考文档混在一起。
5. **每页加面包屑**：页面首行 `> [📖 Docs](../README.md) › <分区> › <页面>`，便于定位与回到索引。
6. **一致应用，避免半套**：约定只有在「处处遵守」时才有信号价值。

## 复制到新仓库

```bash
# 在新仓库根目录执行：创建标准骨架
mkdir -p docs/{overview,guides,reference,conventions}
cat > docs/README.md <<'EOF'
# <Repo> 文档

<一句话定位>。仓库总览见[根 README](../README.md)。

文档按 docs-structure 标准组织：overview / guides / reference / conventions。

## Overview
## Guides
## Reference
## Conventions
## Roadmap
EOF
: > docs/roadmap.md
# 然后把本文件复制为 docs/conventions/docs-structure.md 作为该仓库的标准说明
```

