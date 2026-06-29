# Stage 10: Linear BVH Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the pointer-based recursive BVH into a linear-array structure with flat nodes, indexed primitive references, and stack-based iterative traversal. Rendering output must be pixel-identical to Stage 9.

**Architecture:** Header-only C++17. `LinearBVHNode` is a POD struct (AABB + left/right/prim_start/prim_count/is_leaf). `LinearBVH : public Hittable` builds the tree recursively (midpoint split, sorting primitives in place), stores nodes in a flat `std::vector<LinearBVHNode>`, and traverses with a fixed-depth (64) stack array. `scene.h` only changes the class name `BVHNode` → `LinearBVH`.

**Tech Stack:** C++17, CMake 3.10, header-only. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-06-28-stage10-linear-bvh-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `include/raytracer/geometry/bvh.h` | Rewrite | `LinearBVHNode` POD + `LinearBVH` class (build + stack traversal) |
| `include/raytracer/scene/scene.h` | Modify | `BVHNode` → `LinearBVH` (class name only) |
| `src/main.cpp` | Modify (temp) | Add timing instrumentation for perf comparison; removed in Task 5 |
| `README.md` | Modify | Update status and add BVH section |

---

## Task 1: Rewrite `bvh.h` with LinearBVH

**Files:**
- Rewrite: `include/raytracer/geometry/bvh.h`

- [ ] **Step 1: Write the new file**

Create `include/raytracer/geometry/bvh.h` with this exact content:

```cpp
// Module B: geometry -- linear BVH (flat nodes, stack-based traversal)
#ifndef RT_BVH_H
#define RT_BVH_H

#include "raytracer/geometry/hittable.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cstdint>

// Flat BVH node. POD for easy GPU upload (Stage 12/14).
struct LinearBVHNode {
    AABB box;
    int left = -1;          // internal: left child index; leaf: -1
    int right = -1;         // internal: right child index; leaf: -1
    int prim_start = 0;     // leaf: start index into prim_indices_
    int prim_count = 0;     // leaf: primitive count; internal: 0
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
                if (sp + 2 > MAX_DEPTH) break;  // stack overflow guard
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

    // Recursive build. Returns node index. Sorts primitives_ in [start, end) in place.
    int build(int start, int end) {
        AABB global_box;
        compute_range_box(start, end, global_box);

        int node_idx = static_cast<int>(nodes_.size());
        nodes_.push_back(LinearBVHNode{});
        nodes_[node_idx].box = global_box;

        int span = end - start;
        int axis = global_box.longest_axis();

        if (span == 1) {
            nodes_[node_idx].is_leaf = true;
            nodes_[node_idx].prim_start = static_cast<int>(prim_indices_.size());
            nodes_[node_idx].prim_count = 1;
            prim_indices_.push_back(static_cast<uint32_t>(start));
        } else {
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

- [ ] **Step 2: Compile** (scene.h still references BVHNode, will fail — expected)

Run: `cmake --build build 2>&1 | tail -10`
Expected: error `'BVHNode' was not declared` in scene.h — this is expected, fixed in Task 2.

- [ ] **Step 3: Commit** (intermediate commit, build broken — acceptable per staged plan)

```bash
git add include/raytracer/geometry/bvh.h
git commit -m "Stage 10 task 1: rewrite bvh.h with LinearBVH (build broken, fixed in task 2)"
```

---

## Task 2: Update `scene.h` to use LinearBVH

**Files:**
- Modify: `include/raytracer/scene/scene.h`

- [ ] **Step 1: Find BVHNode usage**

Run: `grep -n "BVHNode" include/raytracer/scene/scene.h`
Expected: one line using `std::make_unique<BVHNode>`

- [ ] **Step 2: Replace BVHNode with LinearBVH**

Replace `std::make_unique<BVHNode>` with `std::make_unique<LinearBVH>` in `scene.h`. The exact line to find and replace:

```cpp
        scene.world = std::make_unique<BVHNode>(scene.primitives.objects);
```

becomes:

```cpp
        scene.world = std::make_unique<LinearBVH>(scene.primitives.objects);
```

- [ ] **Step 3: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer`, no errors (tinyobjloader's 3 unused-function warnings still present, acceptable)

- [ ] **Step 4: Commit**

```bash
git add include/raytracer/scene/scene.h
git commit -m "Stage 10 task 2: use LinearBVH in scene loader"
```

---

## Task 3: Verify rendering consistency with Stage 9

**Files:**
- (none, verification only)

- [ ] **Step 1: Render all five scenes to PPM**

Run:
```bash
./build/raytracer --scene scenes/default.json --out /tmp/s10_default.ppm > /tmp/s10_default.log 2>&1
./build/raytracer --scene scenes/three_balls.json --out /tmp/s10_three.ppm > /tmp/s10_three.log 2>&1
./build/raytracer --scene scenes/triangle_test.json --out /tmp/s10_tri.ppm > /tmp/s10_tri.log 2>&1
./build/raytracer --scene scenes/mark.json --out /tmp/s10_mark.ppm > /tmp/s10_mark.log 2>&1
./build/raytracer --scene scenes/bunny_test.json --out /tmp/s10_bunny.ppm > /tmp/s10_bunny.log 2>&1
./build/raytracer --scene scenes/no_normal_obj.json --out /tmp/s10_cube.ppm > /tmp/s10_cube.log 2>&1
```
Expected: all six render successfully (check each log ends with "Wrote ...")

- [ ] **Step 2: Compare Stage 10 outputs against fresh Stage 9 outputs**

Since we're on the Stage 10 branch, to get Stage 9 baseline we need to checkout the previous commit briefly. Safer approach: compare against the PPMs already rendered in the working directory before this task (if any), OR re-render on a Stage 9 checkout.

Simpler: verify **non-black pixel counts** are reasonable (sanity check) — pixel-exact comparison needs the Stage 9 baseline which we can get by `git stash`-ing changes, but that's complex. For this task, do sanity checks:

Run:
```bash
python3 << 'EOF'
import os
scenes = ['default', 'three', 'tri', 'mark', 'bunny', 'cube']
for s in scenes:
    path = f'/tmp/s10_{s}.ppm'
    if not os.path.exists(path):
        print(f'{s}: MISSING')
        continue
    with open(path) as f:
        parts = f.read().split('\n', 3)
    w, h = map(int, parts[1].split())
    px = list(map(int, parts[3].split()))
    non_black = sum(1 for v in px if v > 10)
    print(f'{s}: {w}x{h}, non-black={100*non_black/len(px):.1f}%')
EOF
```
Expected: percentages match what was observed in Stage 9 verification:
- default: ~100% non-black (sky + 2 spheres on ground)
- three: ~100% non-black
- tri: ~100% non-black
- mark: ~99.6% non-black, ~30% non-sky (from Stage 9 task 8)
- bunny: ~100% non-black, ~12% non-sky (from Stage 9 task 6)
- cube: ~100% non-black, ~50% non-sky (from Stage 9 task 7)

If percentages deviate significantly (>5%), investigate — traversal order may have changed results.

- [ ] **Step 3: (Optional) Pixel-exact comparison with Stage 9**

If you want strict pixel equality, checkout Stage 9 HEAD and render the same scenes, then `diff` the PPMs:

```bash
# Save Stage 10 outputs
mkdir -p /tmp/s10_outputs
cp /tmp/s10_*.ppm /tmp/s10_outputs/

# Checkout Stage 9
git stash push --include-untracked  # if any uncommitted
git checkout 390ee68  # Stage 9 final commit
cmake --build build 2>&1 | tail -3
./build/raytracer --scene scenes/default.json --out /tmp/s9_default.ppm > /dev/null 2>&1
# ... repeat for others ...

# Compare
for s in default three tri mark bunny cube; do
    diff /tmp/s9_${s}.ppm /tmp/s10_outputs/s10_${s}.ppm > /dev/null && echo "$s: IDENTICAL" || echo "$s: DIFFERS"
done

# Return to Stage 10
git checkout feature_hdj
git stash pop  # if stashed
```

This is optional because the spec says results "should be identical" — if Step 2 sanity check passes, pixel-exact is very likely. Skip if time-constrained.

- [ ] **Step 4: No commit** (verification only)

---

## Task 4: Add timing instrumentation and measure performance

**Files:**
- Modify (temp): `src/main.cpp` (add chrono timing around render loop; removed in Task 5)

- [ ] **Step 1: Add timing to main.cpp**

In `src/main.cpp`, add `<chrono>` to the includes, and wrap the render loop (the `for (int j = 0; j < scene.height; j++)` loop) with timing. Find:

```cpp
    std::vector<Color> pixels(scene.width * scene.height);
    int last_pct = -1;

    for (int j = 0; j < scene.height; j++) {
```

Replace with:

```cpp
    std::vector<Color> pixels(scene.width * scene.height);
    int last_pct = -1;

    auto t_start = std::chrono::steady_clock::now();
    for (int j = 0; j < scene.height; j++) {
```

And find the end of the render loop (the line `std::cerr << "\rProgress: 100%\n";`), replace with:

```cpp
    std::cerr << "\rProgress: 100%\n";
    auto t_end = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(t_end - t_start).count();
    std::cerr << "Render time: " << elapsed_sec << " s\n";
```

And add `#include <chrono>` to the includes near the top (after `#include <string>`).

- [ ] **Step 2: Compile**

Run: `cmake --build build 2>&1 | tail -3`
Expected: `Built target raytracer`

- [ ] **Step 3: Measure mark.json (12460 triangles) — run 3 times**

Run:
```bash
for i in 1 2 3; do
    echo "=== Run $i ==="
    ./build/raytracer --scene scenes/mark.json --out /tmp/s10_mark_t.ppm 2>&1 | grep "Render time"
done
```
Expected: 3 "Render time: X s" lines. Note the median.

- [ ] **Step 4: Measure bunny_test.json — run 3 times**

Run:
```bash
for i in 1 2 3; do
    echo "=== Run $i ==="
    ./build/raytracer --scene scenes/bunny_test.json --out /tmp/s10_bunny_t.ppm 2>&1 | grep "Render time"
done
```
Expected: 3 timing lines. Note the median.

- [ ] **Step 5: Record results**

Write down the median times for mark.json and bunny_test.json. These are the LinearBVH baselines. (For comparison with Stage 9 pointer BVH, one would checkout Stage 9 and time the same — optional, as the spec focuses on consistency not speedup. LinearBVH is expected to be similar or slightly faster due to cache locality.)

- [ ] **Step 6: No commit yet** (timing code removed in Task 5)

---

## Task 5: Remove timing instrumentation

**Files:**
- Modify (revert): `src/main.cpp` (remove chrono timing)

- [ ] **Step 1: Revert main.cpp**

Remove the `<chrono>` include, the `auto t_start = ...` line, and the `auto t_end = ...` / `elapsed_sec` / `Render time` lines added in Task 4. Restore the original:

```cpp
    std::vector<Color> pixels(scene.width * scene.height);
    int last_pct = -1;

    for (int j = 0; j < scene.height; j++) {
```

and:

```cpp
    std::cerr << "\rProgress: 100%\n";
```

- [ ] **Step 2: Clean rebuild and verify**

Run:
```bash
rm -rf build && cmake -S . -B build && cmake --build build 2>&1 | tail -3
./build/raytracer --scene scenes/default.json --out /tmp/s10_final.ppm 2>&1 | tail -2
```
Expected: clean build, default scene renders correctly.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "Stage 10 task 5: remove timing instrumentation (Stage 10 complete)"
```

> **Note:** If Task 4's timing was never committed (per "No commit yet" in Step 6), then main.cpp is back to its Task 2 state — `git add src/main.cpp` will show "nothing to commit". In that case, this task's commit is a no-op; just verify the build is clean and proceed. The Stage 10 work is captured in Tasks 1-2 commits.

---

## Task 6: Update README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update status section**

Find the "当前实现状态" list and update:

```markdown
- ✅ 阶段 8：三角形与三角网格求交（Möller–Trumbore + UV 插值 + SoA 网格）
- ✅ 阶段 9：OBJ 模型加载与变换（tinyobjloader + 4x4 矩阵 + 平滑法线）
- ✅ 阶段 10：BVH 线性化加速（扁平节点 + 栈遍历，GPU 友好）
- ⬜ 后续：PBR 材质、CUDA 移植
```

- [ ] **Step 2: Add BVH section (brief)**

After the "## OBJ 网格支持" section, add a new section:

```markdown

## BVH 加速结构

场景图元超过 1 个时自动构建线性 BVH 加速射线求交。

- **线性节点**：`LinearBVHNode` 为 POD 结构（AABB + 左右子索引 + 叶子图元起止），可直接 `memcpy` 到 GPU
- **构建**：中点划分（按最长轴排序取中点），叶子粒度 1 图元
- **遍历**：栈数组迭代（深度 64），与未来 GPU 栈遍历逻辑一致
- **结果一致性**：因 `hit()` 取最近命中，遍历顺序变化不影响渲染结果

后续阶段（12/14）将复用此线性结构与栈遍历逻辑实现 GPU BVH。
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Stage 10 task 6: update README with BVH section and status"
```

---

## Final Verification

- [ ] **Full clean build**

```bash
rm -rf build
cmake -S . -B build
cmake --build build 2>&1 | tail -3
```
Expected: `Built target raytracer`, no new warnings.

- [ ] **Render all scenes**

```bash
./build/raytracer --scene scenes/default.json --out /tmp/final_default.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/three_balls.json --out /tmp/final_three.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/triangle_test.json --out /tmp/final_tri.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/mark.json --out /tmp/final_mark.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/bunny_test.json --out /tmp/final_bunny.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/no_normal_obj.json --out /tmp/final_cube.ppm > /dev/null 2>&1
```
Expected: all six render successfully (check exit codes or file existence).

- [ ] **Verify timing code removed**

Run: `grep -n "chrono\|Render time" src/main.cpp 2>&1 || echo "No timing code (good)"`
Expected: "No timing code (good)".

- [ ] **Git log check**

Run: `git log --oneline -10`
Expected: see Stage 10 commits on top.

---

## Self-Review Notes

- **Spec coverage**: All 8 decisions from spec §3 mapped. Decision 1 (node struct) → Task 1. Decision 2 (indexed prims) → Task 1. Decision 3 (Hittable subclass) → Task 1. Decision 4 (midpoint) → Task 1 build(). Decision 5 (stack traversal) → Task 1 hit(). Decision 6 (delete old BVHNode) → Task 1 rewrite. Decision 7 (depth 64) → Task 1 MAX_DEPTH. Decision 8 (perf verify) → Task 4.
- **Type consistency**: `LinearBVHNode` fields (left/right/prim_start/prim_count/is_leaf) defined in Task 1 match usage in both `hit()` and `build()`. `prim_indices_` is `uint32_t` vector, accessed via `prim_indices_[node.prim_start + i]` consistently.
- **Build correctness**: `build()` sorts `primitives_[start..end)` in place, then recurses. Leaves push to `prim_indices_` the index `start` (which after sorting is the sorted position). `prim_indices_` thus references sorted `primitives_` correctly.
- **Traversal correctness**: Stack pushes left then right; pops right first. `closest` ensures nearest hit wins regardless of traversal order — pixel-identical to Stage 9.
- **No placeholders**: All code complete, all commands exact.
- **Backward compat**: Task 3 verifies all 6 scenes still render with sane pixel counts.
- **Temp artifacts**: Task 4 adds timing, Task 5 removes it. If timing never committed, Task 5 commit is a no-op — acceptable, documented in Step 3 note.
