#include "app/Tab.h"

#include <cmath>

namespace liney {

namespace {

Pane* firstLeaf(Pane* p) {
    while (p && p->isSplit) p = p->a.get();
    return p;
}

void collect(Pane* p, std::vector<Pane*>& out) {
    if (!p) return;
    if (p->leaf()) { out.push_back(p); return; }
    collect(p->a.get(), out);
    collect(p->b.get(), out);
}

void fixParents(Pane* p, Pane* parent) {
    if (!p) return;
    p->parent = parent;
    if (p->isSplit) {
        fixParents(p->a.get(), p);
        fixParents(p->b.get(), p);
    }
}

void layoutRec(Pane* p, const Rect& area, const Metrics& m) {
    p->rect = area;
    if (p->leaf()) {
        if (p->session) {
            // The grid sits inside the pane's inner padding (see
            // Metrics::panePad); size the terminal to the padded area.
            const float pad2 = m.panePad() * 2.0f;
            int cols = static_cast<int>((area.w - pad2) / m.cellW);
            int rows = static_cast<int>((area.h - pad2) / m.cellH);
            if (cols < 1) cols = 1;
            if (rows < 1) rows = 1;
            p->session->resize(cols, rows, static_cast<int>(m.cellW),
                               static_cast<int>(m.cellH));
        }
        return;
    }
    const float g = m.gutter();
    if (p->dir == SplitDir::Cols) {
        float aw = std::floor((area.w - g) * p->ratio);
        if (aw < 0) aw = 0;
        Rect ra{ area.x, area.y, aw, area.h };
        Rect rb{ area.x + aw + g, area.y, area.w - aw - g, area.h };
        layoutRec(p->a.get(), ra, m);
        layoutRec(p->b.get(), rb, m);
    } else {
        float ah = std::floor((area.h - g) * p->ratio);
        if (ah < 0) ah = 0;
        Rect ra{ area.x, area.y, area.w, ah };
        Rect rb{ area.x, area.y + ah + g, area.w, area.h - ah - g };
        layoutRec(p->a.get(), ra, m);
        layoutRec(p->b.get(), rb, m);
    }
}

} // namespace

Tab::Tab(std::unique_ptr<TerminalSession> first) {
    root_ = std::make_unique<Pane>();
    root_->session = std::move(first);
    active_ = root_.get();
}

Tab::Tab(std::unique_ptr<Pane> root) {
    root_ = std::move(root);
    fixParents(root_.get(), nullptr);
    active_ = firstLeaf(root_.get());
}

void Tab::setActive(Pane* leaf) {
    if (leaf && leaf->leaf()) active_ = leaf;
}

void Tab::splitActive(SplitDir dir, std::unique_ptr<TerminalSession> incoming) {
    Pane* p = active_;
    if (!p || !p->leaf()) return;

    auto childA = std::make_unique<Pane>();
    childA->session = std::move(p->session);
    childA->parent = p;

    auto childB = std::make_unique<Pane>();
    childB->session = std::move(incoming);
    childB->parent = p;

    p->isSplit = true;
    p->dir = dir;
    p->ratio = 0.5f;
    active_ = childB.get();
    p->a = std::move(childA);
    p->b = std::move(childB);
}

bool Tab::closeActive() {
    Pane* p = active_;
    if (!p || !p->leaf()) return true;

    Pane* parent = p->parent;
    if (!parent) {
        // Closing the only pane: tab is now empty.
        return false;
    }

    // Take the sibling out, then collapse `parent` into it.
    std::unique_ptr<Pane> sib =
        std::move(parent->a.get() == p ? parent->b : parent->a);
    parent->isSplit = sib->isSplit;
    parent->dir = sib->dir;
    parent->ratio = sib->ratio;
    parent->session = std::move(sib->session);
    parent->a = std::move(sib->a);
    parent->b = std::move(sib->b);
    if (parent->a) parent->a->parent = parent;
    if (parent->b) parent->b->parent = parent;
    // The slots that held `p` and `sib` are overwritten above, freeing both.

    active_ = firstLeaf(parent);
    return true;
}

void Tab::layout(const Rect& area, const Metrics& m) {
    if (root_) layoutRec(root_.get(), area, m);
}

Pane* Tab::hitTest(float x, float y) const {
    std::vector<Pane*> ls;
    collect(root_.get(), ls);
    for (Pane* p : ls)
        if (p->rect.contains(x, y)) return p;
    return nullptr;
}

namespace {
Pane* findDivider(Pane* p, float x, float y, float tol) {
    if (!p || !p->isSplit) return nullptr;
    // Children first so the deepest (most specific) divider wins.
    if (Pane* c = findDivider(p->a.get(), x, y, tol)) return c;
    if (Pane* c = findDivider(p->b.get(), x, y, tol)) return c;
    if (p->dir == SplitDir::Cols) {
        float dx = (p->a->rect.right() + p->b->rect.x) * 0.5f;
        if (std::fabs(x - dx) <= tol && y >= p->rect.y && y <= p->rect.bottom())
            return p;
    } else {
        float dy = (p->a->rect.bottom() + p->b->rect.y) * 0.5f;
        if (std::fabs(y - dy) <= tol && x >= p->rect.x && x <= p->rect.right())
            return p;
    }
    return nullptr;
}
}  // namespace

Pane* Tab::splitDividerAt(float x, float y, float tol) const {
    return findDivider(root_.get(), x, y, tol);
}

void Tab::focusDir(SplitDir axis, bool positive) {
    if (!active_) return;
    std::vector<Pane*> ls;
    collect(root_.get(), ls);

    const float acx = active_->rect.x + active_->rect.w * 0.5f;
    const float acy = active_->rect.y + active_->rect.h * 0.5f;

    Pane* best = nullptr;
    float bestDist = 1e30f;
    for (Pane* p : ls) {
        if (p == active_) continue;
        const float cx = p->rect.x + p->rect.w * 0.5f;
        const float cy = p->rect.y + p->rect.h * 0.5f;
        bool inDir = false;
        if (axis == SplitDir::Cols)
            inDir = positive ? (cx > acx + 1.0f) : (cx < acx - 1.0f);
        else
            inDir = positive ? (cy > acy + 1.0f) : (cy < acy - 1.0f);
        if (!inDir) continue;
        const float dx = cx - acx, dy = cy - acy;
        const float dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = p; }
    }
    if (best) active_ = best;
}

std::vector<Pane*> Tab::leaves() const {
    std::vector<Pane*> ls;
    collect(root_.get(), ls);
    return ls;
}

std::wstring Tab::title() const {
    if (active_ && active_->session) return active_->session->title();
    Pane* f = firstLeaf(root_.get());
    return (f && f->session) ? f->session->title() : L"shell";
}

} // namespace liney
