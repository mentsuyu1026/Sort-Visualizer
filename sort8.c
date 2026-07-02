// Sort Visualizer（1画面）
// - 最大512要素
// - 初期列：昇順/降順/乱数
// - アルゴリズム登録式
// - ボタン操作のみ（キー不要）
// - グラデーション色（Start/End Colorで調整）
// build例:
// gcc sort8.c -o sort8 \
//   -I/usr/local/include -L/usr/local/lib \
//   -lraylib -lm -lpthread -ldl -lrt -lX11

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_N 512
#define PALETTE_MAX 512

// -------------------- UIスケール（ウィンドウサイズ追従） --------------------
// 1280x720 を基準に、ウィンドウサイズに合わせて UI 全体を拡大・縮小する
static float gUiScale = 1.0f;
#define UI(x) ((float)(x) * gUiScale)
static float UiT(float base) { // line thickness helper (clamped)
    float t = base * gUiScale;
    if (t < 1.0f) t = 1.0f;
    if (t > 6.0f) t = 6.0f;
    return t;
}


typedef enum { SEQ_ASC=0, SEQ_DESC=1, SEQ_RAND=2 } Sequence;

// -------------------- フォント --------------------
static Font gFont;
static int gFontLoaded = 0;

static void LoadUIFont(void) {
    const char *candidates[] = {
        "NotoSansJP-Regular.ttf",
        "Roboto-Regular.ttf",
        "DejaVuSans.ttf"
    };
    int n = (int)(sizeof(candidates)/sizeof(candidates[0]));
    for (int i = 0; i < n; i++) {
        if (FileExists(candidates[i])) {
            gFont = LoadFontEx(candidates[i], 28, 0, 0);
            gFontLoaded = 1;
            SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);
            return;
        }
    }
    gFont = GetFontDefault(); // フォールバック
}

static void UnloadUIFont(void) {
    if (gFontLoaded) UnloadFont(gFont);
}

static void DrawTextUI(const char *text, float x, float y, float size, Color col) {
    DrawTextEx(gFont, text, (Vector2){x,y}, size, 1.0f, col);
}

static float TextWidthUI(const char *text, float size) {
    return MeasureTextEx(gFont, text, size, 1.0f).x;
}

static void DrawTextShadow(const char *text, float x, float y, float size, Color col) {
    DrawTextUI(text, x+2, y+2, size, (Color){0,0,0,160});
    DrawTextUI(text, x,   y,   size, col);
}

// -------------------- 色 --------------------
static Color LerpColor(Color a, Color b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    Color c;
    c.r = (unsigned char)(a.r + (b.r - a.r)*t);
    c.g = (unsigned char)(a.g + (b.g - a.g)*t);
    c.b = (unsigned char)(a.b + (b.b - a.b)*t);
    c.a = 255;
    return c;
}

typedef struct {
    Color colors[PALETTE_MAX];
    int count;
    int enabled;
} Palette;

static void PaletteClear(Palette *p) {
    p->count = 0;
    p->enabled = 0;
}

static void PaletteLoadFromFile(Palette *p, const char *path) {
    PaletteClear(p);
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    int r,g,b;
    while (p->count < PALETTE_MAX && fscanf(fp, "%d %d %d", &r, &g, &b) == 3) {
        if (r<0) r=0; if (r>255) r=255;
        if (g<0) g=0; if (g>255) g=255;
        if (b<0) b=0; if (b>255) b=255;
        p->colors[p->count++] = (Color){(unsigned char)r,(unsigned char)g,(unsigned char)b,255};
    }
    fclose(fp);
    if (p->count > 0) p->enabled = 1;
}

// -------------------- 可視化対象（配列＋統計） --------------------
typedef struct {
    int n;
    int a[MAX_N];

    int finished;
    int hi, hj;               // ハイライト用（比較中など）
    long long comparisons;
    long long moves;
} Viz;

// -------------------- アルゴリズム登録式 --------------------
typedef struct Algo {
    const char *name;
    size_t state_size;
    void (*reset)(Viz *v, void *st);
    int  (*step)(Viz *v, void *st); // 1なら終了
} Algo;

// ---- Insertion Sort ----
typedef struct {
    int i;
    int j;
    int key;
    int phase; // 0: key取る, 1: シフト/挿入
} InsertionState;

static void InsertionReset(Viz *v, void *st_) {
    InsertionState *st = (InsertionState*)st_;
    st->i = 1;
    st->phase = 0;
    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int InsertionStep(Viz *v, void *st_) {
    InsertionState *st = (InsertionState*)st_;
    v->hi = v->hj = -1;

    if (st->i >= v->n) {
        v->finished = 1;
        return 1;
    }

    if (st->phase == 0) {
        st->key = v->a[st->i];
        st->j = st->i - 1;
        st->phase = 1;
        return 0;
    }

    if (st->j >= 0) {
        v->comparisons++;
        v->hi = st->j;
        v->hj = st->j + 1;

        if (v->a[st->j] > st->key) {
            v->a[st->j + 1] = v->a[st->j];
            v->moves++;
            st->j--;
            return 0;
        }
    }

    v->a[st->j + 1] = st->key;
    v->moves++;
    st->i++;
    st->phase = 0;
    return 0;
}

// ---- Bubble Sort (O(n^2)) ----
typedef struct { int i, j; } BubbleState;

static void BubbleReset(Viz *v, void *st_) {
    BubbleState *st = (BubbleState*)st_;
    st->i = 0;
    st->j = 0;
    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int BubbleStep(Viz *v, void *st_) {
    BubbleState *st = (BubbleState*)st_;
    v->hi = v->hj = -1;

    if (st->i >= v->n - 1) {
        v->finished = 1;
        return 1;
    }
    if (st->j >= v->n - 1 - st->i) {
        st->i++;
        st->j = 0;
        return 0;
    }

    int j = st->j;
    v->hi = j;
    v->hj = j+1;

    v->comparisons++;
    if (v->a[j] > v->a[j+1]) {
        int tmp = v->a[j];
        v->a[j] = v->a[j+1];
        v->a[j+1] = tmp;
        v->moves++; // swapを1回として数える
    }
    st->j++;
    return 0;
}


// ---- Selection Sort (O(n^2)) ----
typedef struct { int i, j, min; } SelectionState;

static void SelectionReset(Viz *v, void *st_) {
    SelectionState *st = (SelectionState*)st_;
    st->i = 0;
    st->j = 1;
    st->min = 0;
    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int SelectionStep(Viz *v, void *st_) {
    SelectionState *st = (SelectionState*)st_;
    v->hi = v->hj = -1;

    if (v->n <= 1 || st->i >= v->n - 1) {
        v->finished = 1;
        return 1;
    }

    if (st->j < v->n) {
        v->comparisons++;
        v->hi = st->min;
        v->hj = st->j;

        if (v->a[st->j] < v->a[st->min]) st->min = st->j;
        st->j++;
        return 0;
    }

    // swap a[i] and a[min]
    if (st->min != st->i) {
        int tmp = v->a[st->i];
        v->a[st->i] = v->a[st->min];
        v->a[st->min] = tmp;
        v->moves++;
    }
    st->i++;
    st->min = st->i;
    st->j = st->i + 1;
    return 0;
}

// ---- Shell Sort (gap insertion) ----
typedef struct {
    int gap;
    int i;
    int j;
    int temp;
    int phase; // 0: take temp, 1: shift/insert
} ShellState;

static void ShellReset(Viz *v, void *st_) {
    ShellState *st = (ShellState*)st_;
    st->gap = v->n / 2;
    if (st->gap < 1) st->gap = 0;
    st->i = st->gap;
    st->phase = 0;
    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int ShellStep(Viz *v, void *st_) {
    ShellState *st = (ShellState*)st_;
    v->hi = v->hj = -1;

    if (v->n <= 1 || st->gap == 0) {
        v->finished = 1;
        return 1;
    }

    if (st->phase == 0) {
        if (st->i >= v->n) {
            st->gap /= 2;
            if (st->gap == 0) { v->finished = 1; return 1; }
            st->i = st->gap;
            return 0;
        }
        st->temp = v->a[st->i];
        st->j = st->i;
        st->phase = 1;
        return 0;
    }

    if (st->j >= st->gap) {
        v->comparisons++;
        v->hi = st->j - st->gap;
        v->hj = st->j;

        if (v->a[st->j - st->gap] > st->temp) {
            v->a[st->j] = v->a[st->j - st->gap];
            v->moves++;
            st->j -= st->gap;
            return 0;
        }
    }

    v->a[st->j] = st->temp;
    v->moves++;
    st->i++;
    st->phase = 0;
    return 0;
}

// ---- Heap Sort (max-heap) ----
typedef enum { HS_BUILD=0, HS_SWAP=1, HS_SIFT=2 } HeapStage;

typedef struct {
    HeapStage stage;
    int heapSize;
    int start;      // build index
    int end;        // extraction end
    // sift-down state
    int root;
    int child;
    int swapIdx;
    int phase;      // 0: init child, 1: compare left, 2: compare right, 3: swap/check
} HeapState;

static void HeapReset(Viz *v, void *st_) {
    HeapState *st = (HeapState*)st_;
    st->heapSize = v->n;
    st->start = (v->n - 2) / 2;
    st->end = v->n - 1;
    st->stage = HS_BUILD;
    st->root = st->start;
    st->phase = 0;

    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int HeapSiftOne(Viz *v, HeapState *st) {
    // returns 1 if current sift-down finished
    if (st->phase == 0) {
        st->child = 2*st->root + 1;
        if (st->child >= st->heapSize) return 1;
        st->swapIdx = st->root;
        st->phase = 1;
        return 0;
    }
    if (st->phase == 1) {
        v->comparisons++;
        v->hi = st->swapIdx;
        v->hj = st->child;
        if (v->a[st->swapIdx] < v->a[st->child]) st->swapIdx = st->child;
        st->phase = 2;
        return 0;
    }
    if (st->phase == 2) {
        int rchild = st->child + 1;
        if (rchild < st->heapSize) {
            v->comparisons++;
            v->hi = st->swapIdx;
            v->hj = rchild;
            if (v->a[st->swapIdx] < v->a[rchild]) st->swapIdx = rchild;
        }
        st->phase = 3;
        return 0;
    }
    // phase == 3
    v->hi = st->root;
    v->hj = st->swapIdx;
    if (st->swapIdx != st->root) {
        int tmp = v->a[st->root];
        v->a[st->root] = v->a[st->swapIdx];
        v->a[st->swapIdx] = tmp;
        v->moves++;
        st->root = st->swapIdx;
        st->phase = 0;
        return 0;
    }
    // done
    return 1;
}

static int HeapStep(Viz *v, void *st_) {
    HeapState *st = (HeapState*)st_;
    v->hi = v->hj = -1;

    if (v->n <= 1) { v->finished = 1; return 1; }

    if (st->stage == HS_BUILD) {
        if (st->start < 0) {
            st->stage = HS_SWAP;
            return 0;
        }
        if (HeapSiftOne(v, st)) {
            st->start--;
            st->root = st->start;
            st->phase = 0;
        }
        return 0;
    }

    if (st->stage == HS_SWAP) {
        if (st->end <= 0) { v->finished = 1; return 1; }
        int tmp = v->a[0];
        v->a[0] = v->a[st->end];
        v->a[st->end] = tmp;
        v->moves++;

        st->heapSize = st->end; // shrink heap
        st->end--;
        st->root = 0;
        st->phase = 0;
        st->stage = HS_SIFT;
        return 0;
    }

    // HS_SIFT
    if (HeapSiftOne(v, st)) {
        st->stage = HS_SWAP;
    }
    return 0;
}

// ---- Merge Sort (bottom-up) ----
typedef enum { MS_MERGE=0, MS_COPY=1 } MergePhase;

typedef struct {
    int width;
    int left, mid, right;
    int i, j, k;
    int copyPos;
    MergePhase phase;
    int segInited;
    int buf[MAX_N];
} MergeState;

static void MergeReset(Viz *v, void *st_) {
    MergeState *st = (MergeState*)st_;
    st->width = 1;
    st->left = 0;
    st->phase = MS_MERGE;
    st->mid = st->right = 0;
    st->i = st->j = st->k = 0;
    st->copyPos = 0;
    st->segInited = 0;

    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static void MergeSetupSegment(Viz *v, MergeState *st) {
    int n = v->n;
    st->mid = st->left + st->width;
    if (st->mid > n) st->mid = n;
    st->right = st->left + 2*st->width;
    if (st->right > n) st->right = n;
    st->i = st->left;
    st->j = st->mid;
    st->k = st->left;
    st->phase = MS_MERGE;
}

static int MergeStep(Viz *v, void *st_) {
    MergeState *st = (MergeState*)st_;
    v->hi = v->hj = -1;

    if (v->n <= 1) { v->finished = 1; return 1; }
    if (st->width >= v->n) { v->finished = 1; return 1; }

    if (st->left >= v->n) {
        st->width *= 2;
        st->left = 0;
        st->segInited = 0;
        if (st->width >= v->n) { v->finished = 1; return 1; }
    }

    if (!st->segInited) {
        MergeSetupSegment(v, st);
        st->segInited = 1;
    }

    if (st->phase == MS_MERGE) {
        if (st->k >= st->right) {
            st->copyPos = st->left;
            st->phase = MS_COPY;
            return 0;
        }

        int takeLeft = 0;
        if (st->i < st->mid && st->j < st->right) {
            v->comparisons++;
            v->hi = st->i;
            v->hj = st->j;
            takeLeft = (v->a[st->i] <= v->a[st->j]);
        } else if (st->i < st->mid) {
            takeLeft = 1;
            v->hi = st->i;
        } else {
            takeLeft = 0;
            v->hj = st->j;
        }

        if (takeLeft) st->buf[st->k++] = v->a[st->i++];
        else          st->buf[st->k++] = v->a[st->j++];
        v->moves++; // buffer write
        return 0;
    }

    // MS_COPY
    v->hi = st->copyPos;
    v->a[st->copyPos] = st->buf[st->copyPos];
    v->moves++; // write back
    st->copyPos++;
    if (st->copyPos >= st->right) {
        st->left += 2*st->width;
        st->mid = st->right = 0;
        st->i = st->j = st->k = 0;
        st->phase = MS_MERGE;
        st->segInited = 0;
    }
    return 0;
}

// ---- Bin Sort (Counting sort for 1..n) ----
typedef struct {
    int bins[MAX_N];
    int phase;     // 0: count, 1: output
    int i;
    int out;
    int val;
} BinState;

static void BinReset(Viz *v, void *st_) {
    BinState *st = (BinState*)st_;
    memset(st->bins, 0, sizeof(st->bins));
    st->phase = 0;
    st->i = 0;
    st->out = 0;
    st->val = 1;

    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int BinStep(Viz *v, void *st_) {
    BinState *st = (BinState*)st_;
    v->hi = v->hj = -1;

    if (v->n <= 1) { v->finished = 1; return 1; }

    if (st->phase == 0) {
        if (st->i >= v->n) {
            st->phase = 1;
            st->val = 1;
            st->out = 0;
            return 0;
        }
        int x = v->a[st->i];
        if (x < 1) x = 1;
        if (x > v->n) x = v->n;
        st->bins[x - 1] += 1;
        v->hi = st->i;
        st->i++;
        return 0;
    }

    if (st->out >= v->n) {
        v->finished = 1;
        return 1;
    }

    while (st->val <= v->n && st->bins[st->val - 1] == 0) st->val++;
    if (st->val > v->n) { v->finished = 1; return 1; }

    v->a[st->out] = st->val;
    v->moves++;
    v->hi = st->out;

    st->bins[st->val - 1]--;
    st->out++;
    return 0;
}

// ---- Quick Sort（反復＋状態保持） ----
typedef enum { Q_SCAN_I=0, Q_SCAN_J=1, Q_SWAP_OR_DONE=2, Q_PUSH=3 } QPhase;

typedef struct {
    int stack_l[MAX_N];
    int stack_r[MAX_N];
    int top;

    int l, r;
    int i, j;
    int pivot;
    int in_partition;
    QPhase phase;
} QuickState;

static void QuickReset(Viz *v, void *st_) {
    QuickState *st = (QuickState*)st_;
    st->top = 0;
    st->stack_l[st->top] = 0;
    st->stack_r[st->top] = v->n - 1;
    st->top++;

    st->in_partition = 0;
    st->phase = Q_SCAN_I;

    v->finished = 0;
    v->hi = v->hj = -1;
    v->comparisons = 0;
    v->moves = 0;
}

static int QuickStep(Viz *v, void *st_) {
    QuickState *st = (QuickState*)st_;
    v->hi = v->hj = -1;

    if (v->n <= 1) { v->finished = 1; return 1; }

    if (!st->in_partition) {
        // スタックから「未処理区間(l<r)」を1つ取り出す。
        // ※取り出せなければ終了。
        int found = 0;
        while (st->top > 0) {
            st->top--;
            st->l = st->stack_l[st->top];
            st->r = st->stack_r[st->top];
            if (st->l < st->r) { found = 1; break; }
        }
        if (!found) {
            v->finished = 1;
            return 1;
        }

        int mid = (st->l + st->r) / 2;
        st->pivot = v->a[mid];
        st->i = st->l;
        st->j = st->r;
        st->phase = Q_SCAN_I;
        st->in_partition = 1;
        return 0;
    }

    if (st->phase == Q_SCAN_I) {
        // i が右端を越えたら、これ以上 pivot と比較する要素がない
        if (st->i > st->r) { st->phase = Q_SCAN_J; return 0; }
        v->hi = st->i;
        v->comparisons++;
        if (v->a[st->i] < st->pivot) { st->i++; return 0; }
        st->phase = Q_SCAN_J;
        return 0;
    }

    if (st->phase == Q_SCAN_J) {
        // j が左端を越えたら、これ以上 pivot と比較する要素がない
        if (st->j < st->l) { st->phase = Q_SWAP_OR_DONE; return 0; }
        v->hj = st->j;
        v->comparisons++;
        if (v->a[st->j] > st->pivot) { st->j--; return 0; }
        st->phase = Q_SWAP_OR_DONE;
        return 0;
    }

    if (st->phase == Q_SWAP_OR_DONE) {
        v->hi = st->i;
        v->hj = st->j;

        if (st->i <= st->j) {
            if (st->i != st->j) {
                int tmp = v->a[st->i];
                v->a[st->i] = v->a[st->j];
                v->a[st->j] = tmp;
                v->moves++;
            }
            st->i++;
            st->j--;
            st->phase = Q_SCAN_I;
            return 0;
        }

        st->phase = Q_PUSH;
        return 0;
    }

    // Q_PUSH
    {
        int l = st->l, r = st->r, i = st->i, j = st->j;
        if (l < j && st->top < MAX_N) {
            st->stack_l[st->top] = l;
            st->stack_r[st->top] = j;
            st->top++;
        }
        if (i < r && st->top < MAX_N) {
            st->stack_l[st->top] = i;
            st->stack_r[st->top] = r;
            st->top++;
        }
        st->in_partition = 0;
        return 0;
    }
}

static Algo gAlgos[] = {
    { "BubbleSort",    sizeof(BubbleState),    BubbleReset,    BubbleStep    },
    { "SelectionSort", sizeof(SelectionState), SelectionReset, SelectionStep },
    { "InsertionSort", sizeof(InsertionState), InsertionReset, InsertionStep },
    { "ShellSort",     sizeof(ShellState),     ShellReset,     ShellStep     },
    { "QuickSort",     sizeof(QuickState),     QuickReset,     QuickStep     },
    { "HeapSort",      sizeof(HeapState),      HeapReset,      HeapStep      },
    { "MergeSort",     sizeof(MergeState),     MergeReset,     MergeStep     },
    { "BinSort",       sizeof(BinState),       BinReset,       BinStep       },
};
static const int gAlgoCount = (int)(sizeof(gAlgos)/sizeof(gAlgos[0]));

// -------------------- UI部品（ボタン/サイクル） --------------------
static int UiButton(Rectangle r, const char *label, float fontSize) {
    Vector2 m = GetMousePosition();
    int hot = CheckCollisionPointRec(m, r);

    Color bg = hot ? (Color){85,85,85,255} : (Color){55,55,55,255};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, UiT(1), (Color){150,150,150,255});

    float tw = TextWidthUI(label, fontSize);
    DrawTextUI(label, r.x + (r.width - tw)/2, r.y + (r.height - fontSize)/2, fontSize, RAYWHITE);

    return hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static int UiCycle(Rectangle r, const char *title, const char **items, int count, int *index) {
    float fs = UI(18);
    DrawTextUI(title, r.x, r.y - UI(22), fs, (Color){220,220,220,255});
    const char *now = items[*index];
    if (UiButton(r, now, fs)) {
        *index = (*index + 1) % count;
        return 1;
    }
    return 0;
}

static int UiColorBox(Rectangle r, Color *c) {
    Vector2 m = GetMousePosition();
    int hot = CheckCollisionPointRec(m, r);
    DrawRectangleRec(r, *c);
    DrawRectangleLinesEx(r, UiT(2), hot ? RAYWHITE : (Color){120,120,120,255});
    return hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static int UiU8Stepper(Rectangle minusRect, Rectangle valRect, Rectangle plusRect,
                       unsigned char *v, int delta) {
    int changed = 0;

    if (UiButton(minusRect, "-", UI(16))) { 
        int nv = (int)(*v) - delta;
        if (nv < 0) nv = 0;
        *v = (unsigned char)nv;
        changed = 1;
    }
    if (UiButton(plusRect, "+", UI(16)))  { 
        int nv = (int)(*v) + delta;
        if (nv > 255) nv = 255;
        *v = (unsigned char)nv;
        changed = 1;
    }

    DrawRectangleRec(valRect, (Color){55,55,55,255});
    DrawRectangleLinesEx(valRect, UiT(1), (Color){140,140,140,255});

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)(*v));
    // リサイズ時に「-」ボタンの文字が値表示に被って -255 などに見えないよう、
    // 値は valRect の中央に正しくセンタリングする。
    float fs = UI(14);
    float tw = TextWidthUI(buf, fs);
    DrawTextUI(buf,
               valRect.x + (valRect.width  - tw)/2,
               valRect.y + (valRect.height - fs)/2,
               fs,
               RAYWHITE);

    return changed;
}

// RGBを3つ並べたコンパクトなステッパー（R/G/B を + / - で調整）
static int UiRGBSteppers(float x, float y, float width, Color *c) {
    const float labelFS = UI(14.0f);
    const float gap = UI(6.0f);
    float groupW = (width - 2*gap) / 3.0f;

    int changed = 0;

    // R
    DrawTextUI("R", x, y, labelFS, (Color){220,220,220,255});
    Rectangle rMinus = (Rectangle){ x,            y + UI(16), UI(18), UI(22) };
    Rectangle rPlus  = (Rectangle){ x + groupW-UI(18), y + UI(16), UI(18), UI(22) };
    // NOTE: ここがスケール非対応だと拡大時に値枠が小さいままになり、
    // 「-」ボタンの表示が値に重なって -255 のように見える。
    Rectangle rVal   = (Rectangle){ x + UI(20),       y + UI(16), groupW - UI(40), UI(22) };
    changed |= UiU8Stepper(rMinus, rVal, rPlus, &c->r, 5);

    // G
    float gx = x + groupW + gap;
    DrawTextUI("G", gx, y, labelFS, (Color){220,220,220,255});
    Rectangle gMinus = (Rectangle){ gx,            y + UI(16), UI(18), UI(22) };
    Rectangle gPlus  = (Rectangle){ gx + groupW-UI(18), y + UI(16), UI(18), UI(22) };
    Rectangle gVal   = (Rectangle){ gx + UI(20),       y + UI(16), groupW - UI(40), UI(22) };
    changed |= UiU8Stepper(gMinus, gVal, gPlus, &c->g, 5);

    // B
    float bx = x + 2*(groupW + gap);
    DrawTextUI("B", bx, y, labelFS, (Color){220,220,220,255});
    Rectangle bMinus = (Rectangle){ bx,            y + UI(16), UI(18), UI(22) };
    Rectangle bPlus  = (Rectangle){ bx + groupW-UI(18), y + UI(16), UI(18), UI(22) };
    Rectangle bVal   = (Rectangle){ bx + UI(20),       y + UI(16), groupW - UI(40), UI(22) };
    changed |= UiU8Stepper(bMinus, bVal, bPlus, &c->b, 5);

    return changed;
}

static Color NextPresetColor(Color cur) {
    Color presets[] = {
        (Color){255,255,0,255},   // yellow
        (Color){255,165,0,255},   // orange
        (Color){255,  0,0,255},   // red
        (Color){  0,255,0,255},   // green
        (Color){  0,160,255,255}, // sky
        (Color){ 80,  0,255,255}, // purple
        (Color){255,255,255,255}  // white
    };
    int n = (int)(sizeof(presets)/sizeof(presets[0]));
    for (int i = 0; i < n; i++) {
        if (cur.r==presets[i].r && cur.g==presets[i].g && cur.b==presets[i].b) {
            return presets[(i+1)%n];
        }
    }
    return presets[0];
}

// -------------------- 共有の「初期列生成」 --------------------
static void GenerateBaseArray(int *out, int n, Sequence seq) {
    if (seq == SEQ_ASC) {
        for (int i = 0; i < n; i++) out[i] = i + 1;
    } else if (seq == SEQ_DESC) {
        for (int i = 0; i < n; i++) out[i] = n - i;
    } else {
        for (int i = 0; i < n; i++) out[i] = i + 1;
        for (int i = n - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = out[i]; out[i] = out[j]; out[j] = tmp;
        }
    }
}

// -------------------- 可視化：棒描画 --------------------
typedef struct {
    Viz v;
    int base[MAX_N];       // 比較用に「初期列」を保存
    int algoIndex;
    void *algoState;

    int playing;
    float stepsPerSec;
    float acc;

    // 色
    Color startColor;
    Color endColor;
} App;

static void AppResetToBase(App *a) {
    for (int i = 0; i < a->v.n; i++) a->v.a[i] = a->base[i];
    gAlgos[a->algoIndex].reset(&a->v, a->algoState);
    a->playing = 0;
    a->acc = 0;
}

static void AppSetAlgo(App *a, int newIndex) {
    a->algoIndex = newIndex;
    free(a->algoState);
    a->algoState = calloc(1, gAlgos[a->algoIndex].state_size);
    AppResetToBase(a);
}

static Color AppColorForValue(const App *a, int val) {
    int n = a->v.n;
    if (n <= 1) return WHITE;

    float t = (float)(val - 1) / (float)(n - 1);
    return LerpColor(a->startColor, a->endColor, t);
}
static void AppUpdate(App *a) {
    if (!a->playing) return;
    if (a->v.finished) return;

    float dt = GetFrameTime();
    a->acc += dt * a->stepsPerSec;

    int guard = 0;
    while (a->acc >= 1.0f && !a->v.finished && guard < 20000) {
        gAlgos[a->algoIndex].step(&a->v, a->algoState);
        a->acc -= 1.0f;
        guard++;
    }
}

static void DrawBars(const App *a, Rectangle plot) {
    DrawRectangleRec(plot, (Color){45,45,45,255});
    DrawRectangleLinesEx(plot, UiT(2), (Color){110,110,110,255});

    int n = a->v.n;
    float bw = plot.width / (float)n;
    if (bw < 1) bw = 1;

    for (int i = 0; i < n; i++) {
        int val = a->v.a[i];
        if (val < 1) val = 1;
        if (val > n) val = n;

        float t = (float)val / (float)n;
        float h = (plot.height - UI(6)) * t;

        float x = plot.x + i * (plot.width / (float)n);
        float y = plot.y + plot.height - h;

        Color c = AppColorForValue(a, val);
        if (i == a->v.hi || i == a->v.hj) c = RAYWHITE;

        DrawRectangle((int)x, (int)y, (int)bw, (int)h, c);
    }
}

int main(void) {
    srand((unsigned)time(NULL));

    const int BASE_W = 1280;
    const int BASE_H = 720;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(BASE_W, BASE_H, "Sorting Visualizer");
    SetWindowMinSize(960, 540);
    LoadUIFont();
    SetTargetFPS(60);


    // 初期設定
    App app;
    memset(&app, 0, sizeof(app));
    app.v.n = 128;
    app.algoIndex = 0; // InsertionSort
    app.algoState = calloc(1, gAlgos[app.algoIndex].state_size);
    app.playing = 0;
    app.stepsPerSec = 800.0f;
    app.acc = 0;
    app.startColor = (Color){255,255,0,255}; // yellow
    app.endColor   = (Color){255,  0,0,255}; // red
    Sequence seq = SEQ_RAND;
    GenerateBaseArray(app.base, app.v.n, seq);
    for (int i = 0; i < app.v.n; i++) app.v.a[i] = app.base[i];
    gAlgos[app.algoIndex].reset(&app.v, app.algoState);

    const char *seqItems[] = { "Ascending", "Descending", "Random" };

    while (!WindowShouldClose()) {
    AppUpdate(&app);

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    // 1280x720 を基準に UI スケールを決定（最大化/リサイズで棒グラフと設定パネルも拡大）
    float sW = (float)screenW / (float)BASE_W;
    float sH = (float)screenH / (float)BASE_H;
    gUiScale = (sW < sH) ? sW : sH;
    if (gUiScale < 0.70f) gUiScale = 0.70f;
    if (gUiScale > 2.00f) gUiScale = 2.00f;

    float margin = UI(12);
    float bottomBandH = UI(90);
    float gap = UI(12);

    Rectangle mainArea = (Rectangle){ margin, margin,
        (float)screenW - 2*margin,
        (float)screenH - bottomBandH - 2*margin
    };

    // 右パネル幅もスケール（小さいウィンドウでは最低限を確保）
    float ctrlW = UI(240);
    float minCtrlW = UI(200);
    float minPlotW = UI(260);

    float availPlotW = mainArea.width - ctrlW - UI(30) - gap;
    if (availPlotW < minPlotW) {
        ctrlW = mainArea.width - minPlotW - UI(30) - gap;
        if (ctrlW < minCtrlW) ctrlW = minCtrlW;
    }

    Rectangle plot = (Rectangle){
        mainArea.x + UI(10),
        mainArea.y + UI(10),
        mainArea.width - ctrlW - UI(30) - gap,
        mainArea.height - UI(20)
    };
    if (plot.width < UI(80)) plot.width = UI(80);

    Rectangle ctrl = (Rectangle){
        plot.x + plot.width + gap,
        mainArea.y + UI(10),
        ctrlW,
        mainArea.height - UI(20)
    };

    float fs16 = UI(16);
    float fs18 = UI(18);
    float fs20 = UI(20);
    float fs44 = UI(44);

    BeginDrawing();
    ClearBackground((Color){30,30,30,255});

    // 下帯（タイトル）
    Rectangle band = (Rectangle){0, (float)screenH - bottomBandH, (float)screenW, bottomBandH};
    DrawRectangleRec(band, (Color){10,10,10,255});

    // 背景枠
    DrawRectangleRec(mainArea, (Color){70,70,70,255});
    DrawRectangleLinesEx(mainArea, UiT(2), (Color){120,120,120,255});

    // 棒グラフ
    DrawBars(&app, plot);

    // 右パネル
    DrawRectangleRec(ctrl, (Color){75,75,75,255});
    DrawRectangleLinesEx(ctrl, UiT(2), (Color){130,130,130,255});

    float x = ctrl.x + UI(14);
    float y = ctrl.y + UI(14);

    DrawTextUI("Number", x, y, fs18, RAYWHITE); y += UI(22);

    // Number: - / value / +
    Rectangle bnMinus = (Rectangle){ x, y, UI(44), UI(34) };
    Rectangle bnPlus  = (Rectangle){ x + UI(160), y, UI(44), UI(34) };
    Rectangle bnVal   = (Rectangle){ x + UI(50), y, UI(104), UI(34) };

    if (UiButton(bnMinus, "-", fs20)) {
        app.v.n -= 8; if (app.v.n < 16) app.v.n = 16;
    }
    if (UiButton(bnPlus, "+", fs20)) {
        app.v.n += 8; if (app.v.n > 512) app.v.n = 512;
    }

    DrawRectangleRec(bnVal, (Color){55,55,55,255});
    DrawRectangleLinesEx(bnVal, UiT(1), (Color){140,140,140,255});
    char nb[32];
    snprintf(nb, sizeof(nb), "%d", app.v.n);
    float tw = TextWidthUI(nb, fs18);
    DrawTextUI(nb, bnVal.x + (bnVal.width - tw)/2, bnVal.y + UI(8), fs18, RAYWHITE);

    y += UI(52);

    // Sequence
    int seqIndex = (int)seq;
    Rectangle seqRect = (Rectangle){ x, y + UI(22), ctrlW - UI(28), UI(34) };
    UiCycle(seqRect, "Sequence", seqItems, 3, &seqIndex);
    seq = (Sequence)seqIndex;
    y += UI(78);

    // Generate（baseを作り直して Reset）
    Rectangle genRect = (Rectangle){ x, y, ctrlW - UI(28), UI(36) };
    if (UiButton(genRect, "Generate", fs18)) {
        GenerateBaseArray(app.base, app.v.n, seq);
        for (int i = 0; i < app.v.n; i++) app.v.a[i] = app.base[i];
        AppResetToBase(&app);
    }
    y += UI(52);

    // Kind（クリックで巡回）
    DrawTextUI("Kind", x, y, fs18, RAYWHITE); y += UI(22);
    Rectangle kindRect = (Rectangle){ x, y, ctrlW - UI(28), UI(34) };
    if (UiButton(kindRect, gAlgos[app.algoIndex].name, fs18)) {
        int next = (app.algoIndex + 1) % gAlgoCount;
        AppSetAlgo(&app, next);
    }
    y += UI(52);

    // Animate / Pause
    Rectangle animRect  = (Rectangle){ x, y, (ctrlW - UI(34))/2, UI(34) };
    Rectangle pauseRect = (Rectangle){ x + (ctrlW - UI(34))/2 + UI(6), y, (ctrlW - UI(34))/2, UI(34) };
    if (UiButton(animRect, "Animate", fs18)) app.playing = 1;
    if (UiButton(pauseRect, "Pause", fs18))  app.playing = 0;
    y += UI(44);

    // Step / Reset to Base
    Rectangle stepRect = (Rectangle){ x, y, ctrlW - UI(28), UI(34) };
    if (UiButton(stepRect, "Step (1)", fs18)) {
        if (!app.v.finished) gAlgos[app.algoIndex].step(&app.v, app.algoState);
    }
    y += UI(44);

    Rectangle resetRect = (Rectangle){ x, y, ctrlW - UI(28), UI(34) };
    if (UiButton(resetRect, "Reset to Base", fs18)) {
        AppResetToBase(&app);
    }
    y += UI(54);

    // Colors (Start/End)
    DrawTextUI("Start Color", x, y, fs16, RAYWHITE);
    Rectangle c1 = (Rectangle){ x + UI(120), y - UI(2), UI(70), UI(20) };
    if (UiColorBox(c1, &app.startColor)) app.startColor = NextPresetColor(app.startColor);
    y += UI(28);
    UiRGBSteppers(x, y, ctrlW - UI(28), &app.startColor);
    y += UI(52);

    DrawTextUI("End Color", x, y, fs16, RAYWHITE);
    Rectangle c2 = (Rectangle){ x + UI(120), y - UI(2), UI(70), UI(20) };
    if (UiColorBox(c2, &app.endColor)) app.endColor = NextPresetColor(app.endColor);
    y += UI(28);
    UiRGBSteppers(x, y, ctrlW - UI(28), &app.endColor);
    y += UI(44);

    // 統計（下帯に表示）
    char s1[128], s2[128], s3[128];
    snprintf(s1, sizeof(s1), "comparisons: %lld", app.v.comparisons);
    snprintf(s2, sizeof(s2), "moves:       %lld", app.v.moves);
    snprintf(s3, sizeof(s3), "%s", app.v.finished ? "DONE" : (app.playing ? "PLAYING" : "PAUSED"));

    float sx = band.x + UI(18);
    float sy = band.y + UI(10);
    DrawTextUI(s1, sx, sy, fs18, RAYWHITE);
    DrawTextUI(s2, sx, sy + UI(22), fs18, RAYWHITE);

    float sw = TextWidthUI(s3, fs20);
    Color sc = app.v.finished ? GREEN : (app.playing ? GREEN : ORANGE);
    DrawTextUI(s3, band.x + band.width - sw - UI(18), sy + UI(18), fs20, sc);

    // 下帯に大きくアルゴ名
    const char *title = gAlgos[app.algoIndex].name;
    float tW = TextWidthUI(title, fs44);
    float centerX = screenW / 2.0f;
    float ty = (float)screenH - bottomBandH + UI(20);
    DrawTextShadow(title, centerX - tW/2, ty, fs44, RAYWHITE);

    EndDrawing();
}

    free(app.algoState);
    UnloadUIFont();
    CloseWindow();
    return 0;
}
