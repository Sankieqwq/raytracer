# 阶段 10：BVH 线性化加速结构 — 设计文档

- **日期**：2026-06-28
- **阶段**：Stage 10（Linear BVH Acceleration）
- **状态**：Draft（待用户 review）
- **作者**：hudaijin
- **关联总体规划**：CPU 光线追踪器 → PBR/BVH/CUDA 离线渲染器扩展（阶段 8-16）
- **前置阶段**：阶段 8（三角形/网格）、阶段 9（OBJ + Mat4）已完成

---

## 1. 背景与目标

当前 `bvh.h` 是**指针式递归 BVH**（`BVHNode : public Hittable`，`unique_ptr<BVHNode>` 子节点），不满足后续 CUDA 移植需求：
- 节点间用指针连接，无法整体 `memcpy` 到 GPU
- 递归遍历，GPU 上需用栈模拟
- 叶子存 `Hittable*` 裸指针，GPU 上无意义

阶段 10 目标：**将 BVH 重构为线性数组结构**（扁平化节点 + 索引化图元引用 + 栈数组遍历），保持渲染结果与阶段 9 完全一致，且为阶段 12 GPU SoA 重构和阶段 14 CUDA BVH 遍历做好铺垫。

### 1.1 阶段 10 验收标准（来自总体任务）

- [ ] BVH 节点支持序列化为**线性数组**（扁平化）
- [ ] 每节点存储 AABB、左右子节点索引（或叶子图元起止索引）、是否叶子标志
- [ ] CPU 端遍历用栈数组（深度 64），与未来 GPU 版本逻辑一致
- [ ] BVH 接受一组图元（三角形 + 球体统一抽象为带 AABB 的图元）
- [ ] 替换 hittable_list / 指针 BVH 作为主求交结构
- [ ] 中点划分（SAH 留作后续）
- [ ] BVH 开启/关闭结果一致（仅性能差异）
- [ ] 百万级三角形场景能在合理时间内渲染（mark.obj 12460 三角形作为基线）

---

## 2. 现状分析

### 2.1 现有 `bvh.h`（指针式递归）

```
BVHNode : public Hittable
├── const Hittable* left_           // 叶子时指向单个图元
├── const Hittable* right_
├── unique_ptr<BVHHNode> left_node_  // 内部节点时指向子树
├── unique_ptr<BVHHNode> right_node_
└── AABB box_
```

问题：
1. 节点用 `unique_ptr` 散布在堆上，非连续
2. 遍历递归（`left_child()->hit()` 递归调用）
3. 叶子存 `Hittable*`，无法迁移到 GPU
4. 构建：中点划分（按最长轴排序取中点）—— 逻辑可复用，但数据结构要换

### 2.2 现有图元类型（均继承 `Hittable`，均有 `bounding_box`）

| 类型 | 文件 | bounding_box |
|------|------|--------------|
| `Sphere` | `sphere.h` | ✅ |
| `Triangle` | `triangle.h` | ✅ |
| `TriangleMesh` | `triangle_mesh.h` | ✅（惰性 mutable） |

### 2.3 `scene.h` 中的使用

```cpp
scene.world = std::make_unique<BVHNode>(scene.primitives.objects);
```

`scene.primitives.objects` 是 `std::vector<Hittable*>`（指向 `scene.objects` 持有的 `unique_ptr<Hittable>`）。

---

## 3. 关键设计决策（已确认）

### 决策 1：线性 BVH 节点结构 — **标准 48 字节（选项 B）**

```cpp
struct LinearBVHNode {
    AABB box;              // 48 字节（6 doubles）
    int left = -1;         // 内部节点：左子节点索引；叶子：-1
    int right = -1;        // 内部节点：右子节点索引；叶子：-1
    int prim_start = 0;    // 叶子：图元索引数组起始
    int prim_count = 0;    // 叶子：图元数量；内部：0
    bool is_leaf = false;  // 是否叶子
};
```

**理由**：字段语义清晰，阶段 12 可直接 `memcpy` 到 GPU。紧凑位复用（32B）可读性差，且 CUDA 对齐要求下未必省内存。

### 决策 2：图元索引化 — **叶子存 `uint32_t` 索引（选项 B）**

```cpp
class LinearBVH : public Hittable {
    std::vector<LinearBVHNode> nodes_;      // 扁平节点数组
    std::vector<Hittable*> primitives_;     // 重排后的图元指针数组（CPU 用）
    std::vector<uint32_t> prim_indices_;    // 叶子引用的图元索引
};
```

叶子节点的 `prim_start..prim_start+prim_count` 指向 `prim_indices_`，后者索引 `primitives_`。

**理由**：GPU 上指针无意义，索引可整体上传。构建时按 BVH 叶子顺序重排 `prim_indices_`，提升缓存局部性。

### 决策 3：LinearBVH 继承 Hittable — **保持子类（选项 A）**

```cpp
class LinearBVH : public Hittable {
public:
    explicit LinearBVH(std::vector<Hittable*> objects);
    bool hit(...) const override;          // 栈遍历
    bool bounding_box(AABB& out) const override;
};
```

**理由**：`scene.world` 类型不变（`unique_ptr<Hittable>`），最小侵入。阶段 12 去多态时再改。

### 决策 4：构建算法 — **中点划分（选项 A）**

复用现有逻辑：按最长轴排序，取中点递归划分。SAH 留作后续优化。

**理由**：任务明确"先实现中点划分，再可选 SAH"，本阶段聚焦线性化。

### 决策 5：遍历方式 — **栈数组迭代（选项 B）**

```cpp
bool hit(const Ray& r, double t_min, double t_max, HitRecord& rec) const override {
    const int MAX_DEPTH = 64;
    int stack[MAX_DEPTH];
    int sp = 0;
    stack[sp++] = 0;  // root
    bool hit_any = false;
    double closest = t_max;
    while (sp > 0) {
        int idx = stack[--sp];
        const LinearBVHNode& node = nodes_[idx];
        if (!node.box.hit(r, t_min, closest)) continue;
        if (node.is_leaf) {
            for (int i = 0; i < node.prim_count; i++) {
                uint32_t pi = prim_indices_[node.prim_start + i];
                HitRecord tmp;
                if (primitives_[pi]->hit(r, t_min, closest, tmp)) {
                    rec = tmp;
                    closest = tmp.t;
                    hit_any = true;
                }
            }
        } else {
            if (sp + 2 > MAX_DEPTH) break;  // 栈溢出保护
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }
    return hit_any;
}
```

**理由**：与未来 GPU 栈遍历逻辑一致（GPU 用本地数组模拟栈），阶段 14 直接复用。深度 64 足够（2^64 节点）。

### 决策 6：删除旧 BVHNode — **删除（选项 A）**

`bvh.h` 删除 `BVHNode` 类，改为只包含 `LinearBVH`。或直接重写 `bvh.h` 为 `LinearBVH`。

**理由**：避免维护两套。旧的不满足要求，无保留价值。

### 决策 7：栈深度 — **64（选项 A）**

`constexpr int MAX_DEPTH = 64;`

**理由**：行业惯例，足够深 BVH。

### 决策 8：性能验证 — **渲染一致 + 耗时对比（选项 A）**

- 渲染 `bunny_test.json`（mark.obj 12460 三角形）对比阶段 9 输出像素
- 打印渲染耗时，对比指针 BVH（阶段 9）vs LinearBVH（阶段 10）

---

## 4. 详细设计

### 4.1 `LinearBVHNode` 结构

```cpp
struct LinearBVHNode {
    AABB box;
    int left = -1;
    int right = -1;
    int prim_start = 0;
    int prim_count = 0;
    bool is_leaf = false;
};
```

### 4.2 `LinearBVH` 类（重写 `bvh.h`）

**构建策略**：递归构建，构建过程中**不重排** `primitives_`（保持原顺序），而是用 `prim_indices_` 数组记录每个叶子包含的图元索引。遍历时 `node.prim_start + i` 索引 `prim_indices_`，后者再索引 `primitives_`。这样叶子引用的图元可能不连续，但构建简单可靠，重排可在阶段 12 优化。

文件：`include/raytracer/geometry/bvh.h`（重写）

```cpp
#ifndef RT_BVH_H
#define RT_BVH_H

#include "raytracer/geometry/hittable.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cstdint>

struct LinearBVHNode {
    AABB box;
    int left = -1;
    int right = -1;
    int prim_start = 0;   // 叶子：prim_indices_ 中的起始
    int prim_count = 0;
    bool is_leaf = false;
};

class LinearBVH : public Hittable {
public:
    explicit LinearBVH(std::vector<Hittable*> objects) {
        if (objects.empty()) {
            throw std::runtime_error("LinearBVH requires at least one object");
        }
        primitives_ = std::move(objects);
        nodes_.reserve(2 * primitives_.size() - 1);
        build(0, static_cast<int>(primitives_.size()));
    }

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        if (nodes_.empty()) return false;
        constexpr int MAX_DEPTH = 64;
        int stack[MAX_DEPTH];
        int sp = 0;
        stack[sp++] = 0;
        bool hit_any = false;
        double closest = t_max;
        while (sp > 0) {
            int idx = stack[--sp];
            const LinearBVHNode& node = nodes_[idx];
            if (!node.box.hit(r, t_min, closest)) continue;
            if (node.is_leaf) {
                for (int i = 0; i < node.prim_count; i++) {
                    uint32_t pi = prim_indices_[node.prim_start + i];
                    HitRecord tmp;
                    if (primitives_[pi]->hit(r, t_min, closest, tmp)) {
                        rec = tmp;
                        closest = tmp.t;
                        hit_any = true;
                    }
                }
            } else {
                if (sp + 2 > MAX_DEPTH) break;
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        }
        return hit_any;
    }

    bool bounding_box(AABB& output_box) const override {
        if (nodes_.empty()) return false;
        output_box = nodes_[0].box;
        return true;
    }

private:
    std::vector<LinearBVHNode> nodes_;
    std::vector<Hittable*> primitives_;
    std::vector<uint32_t> prim_indices_;

    // 返回新节点索引。叶子的 prim_start 指向 prim_indices_ 当前末尾。
    int build(int start, int end) {
        AABB global_box;
        compute_range_box(start, end, global_box);

        int node_idx = static_cast<int>(nodes_.size());
        nodes_.push_back(LinearBVHNode{});
        nodes_[node_idx].box = global_box;

        int span = end - start;
        int axis = global_box.longest_axis();

        if (span == 1) {
            // 叶子：单个图元
            nodes_[node_idx].is_leaf = true;
            nodes_[node_idx].prim_start = static_cast<int>(prim_indices_.size());
            nodes_[node_idx].prim_count = 1;
            prim_indices_.push_back(static_cast<uint32_t>(start));
        } else {
            // 中点划分：按最长轴排序 [start, end) 段的图元
            std::sort(primitives_.begin() + start, primitives_.begin() + end,
                [axis](const Hittable* a, const Hittable* b) {
                    AABB ba, bb;
                    a->bounding_box(ba);
                    b->bounding_box(bb);
                    if (axis == 0) return ba.minimum.x < bb.minimum.x;
                    if (axis == 1) return ba.minimum.y < bb.minimum.y;
                    return ba.minimum.z < bb.minimum.z;
                });
            int mid = start + span / 2;
            int left = build(start, mid);
            int right = build(mid, end);
            nodes_[node_idx].left = left;
            nodes_[node_idx].right = right;
        }
        return node_idx;
    }

    void compute_range_box(int start, int end, AABB& out) {
        bool first = true;
        AABB tmp;
        for (int i = start; i < end; i++) {
            primitives_[i]->bounding_box(tmp);
            out = first ? tmp : AABB::surrounding_box(out, tmp);
            first = false;
        }
    }
};

#endif
```

**关键点**：
- 构建时 `std::sort` 直接重排 `primitives_[start..end)` 段（原地），叶子 `prim_indices_` 记录重排后的位置
- 叶子节点的 `prim_start` 指向 `prim_indices_` 末尾，`prim_count` 为叶子图元数（通常 1，可选更大阈值但本阶段用 1）
- 栈遍历：先 push left 再 push right，弹出顺序是 right 先 left 后——但因 `closest` 取最近，结果与递归一致

### 4.3 `scene.h` 修改

`BVHNode` → `LinearBVH`：

```cpp
#include "raytracer/geometry/bvh.h"  // 已有，但内容变了
...
scene.world = std::make_unique<LinearBVH>(scene.primitives.objects);
```

仅需改类名（`BVHNode` → `LinearBVH`），其余不变。

---

## 5. 验收场景

### 5.1 渲染一致性验证

五场景（default/three_balls/triangle_test/mark/bunny_test/no_normal_obj）渲染输出与阶段 9 像素级一致（同 seed 同采样数）。

> **注意**：现有 `random_double()` 用 `static std::mt19937`，跨运行结果一致；但 BVH 遍历顺序变化可能影响"同 t 值多命中"时的选择（取第一个 vs 最近）。当前 `hit()` 已用 `closest` 取最近，所以遍历顺序不影响结果——**应完全一致**。

### 5.2 性能对比

`bunny_test.json`（mark.obj 12460 三角形）渲染耗时：
- 阶段 9（指针 BVH）：基线
- 阶段 10（LinearBVH）：应相近或更优（线性数组缓存友好）

打印耗时：在 `main.cpp` 渲染循环前后加 `std::chrono` 计时（临时，验收后移除）。

---

## 6. 不在本阶段范围

- SAH 表面积启发式划分（后续优化）
- GPU BVH 遍历（阶段 14）
- 图元去多态 SoA 重构（阶段 12）
- 多线程并行构建/遍历

---

## 7. 风险与注意事项

1. **栈溢出**：MAX_DEPTH=64，若 BVH 深度超过 64 会 `break`（漏命中）。中点划分下 BVH 深度 = log2(N)，12460 三角形约 14 层，远小于 64。百万三角形约 20 层，仍安全。
2. **遍历顺序一致性**：栈遍历是 DFS（先入后出），`stack[sp++] = left; stack[sp++] = right;` 先访问 right 再 left（因为 right 后入先出）。这与递归的 left-first 不同，但因 `closest` 取最近，结果应一致。**需验证像素一致**。
3. **图元重排副作用**：构建时对 `primitives_` 段重排，但 `scene.objects` 仍持有 `unique_ptr`，`primitives_` 是裸指针副本，重排不影响所有权。**需确认 `scene.primitives.objects` 在 BVH 构建后不再被使用**（检查 `scene.h`：`primitive_count` 在 BVH 构建前赋值，之后不用 primitives，安全）。
4. **空场景**：`LinearBVH` 构造函数抛异常（与旧 BVHNode 一致）。
5. **性能测量噪声**：单次渲染受系统调度影响，建议跑 3 次取中位数。

---

## 8. 实施顺序

1. 重写 `include/raytracer/geometry/bvh.h`（LinearBVHNode + LinearBVH）
2. 修改 `include/raytracer/scene/scene.h`（`BVHNode` → `LinearBVH`）
3. 编译 + 五场景渲染一致性验证
4. 临时加计时到 `main.cpp`，跑 mark.json 性能对比
5. 移除计时代码
6. 更新 `README.md`
7. Git commit

---

## 9. 验证清单

- [ ] `cmake -S . -B build && cmake --build build` 编译通过
- [ ] 五场景渲染输出与阶段 9 像素一致（对比 PPM 文件）
- [ ] mark.json 渲染耗时打印（LinearBVH vs 指针 BVH 基线）
- [ ] README「当前实现状态」更新阶段 10 完成项
- [ ] Git commit：`Stage 10: linear BVH with stack-based traversal`

---

## 10. 后续阶段衔接

| 后续阶段 | 本阶段如何铺垫 |
|---------|---------------|
| 阶段 12（GPU SoA） | `LinearBVHNode` 是 POD，可直接 `memcpy` 到 GPU；`prim_indices_` 是 `uint32_t` 数组，GPU 友好 |
| 阶段 14（CUDA BVH 遍历） | 栈遍历逻辑（`RT_HOSTDEV` 修饰后）直接复用，GPU 用本地数组栈 |
| 阶段 15（优化） | 可加 SAH 划分、排序启发式（Morton code）等，不破坏线性结构 |
