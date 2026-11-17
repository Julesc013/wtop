/* Exercise the production native tree builder without copying its algorithm.
   The renamed application entry point is linked but never entered.
   SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#error Native tree fixtures require assertions in Release builds too.
#endif
#define WTOP_TREE_TESTING 1
#define wmain wtop_application_main
#include "../source/ntop.c"
#undef wmain

static void reset_rows(DWORD count)
{
    memset(ProcessList, 0, (size_t)count * sizeof(*ProcessList));
    ProcessCount = count;
    TreeFailIndexAllocation = FALSE;
}

static void test_wide_tree(void)
{
    reset_rows(MAX_PROCESS_ROWS);
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].ID = i + 1;
        ProcessList[i].CreationTime = i + 1;
        ProcessList[i].ParentPID = i ? 1 : 0;
    }
    ULONGLONG started = GetTickCount64();
    FindParentChildProcesses();
    printf("native tree: %lu-row star constructed in %llu ms\n", ProcessCount,
        (unsigned long long)(GetTickCount64() - started));
    assert(!TreeIndexUnavailable && !TreeUnresolvedParentCount);
    assert(RootProcess.FirstChild == ProcessList && RootProcess.LastChild == ProcessList);
    assert(!ProcessList[0].TreeDepth && !ProcessList[0].Next);
    process *child = ProcessList[0].FirstChild;
    for(DWORD i = 1; i < ProcessCount; ++i) {
        assert(child == &ProcessList[i]);
        assert(child->Parent == ProcessList && child->TreeDepth == 1 && !child->FirstChild);
        child = child->Next;
    }
    assert(!child && ProcessList[0].LastChild == &ProcessList[ProcessCount - 1]);
}

static void test_deep_reverse_tree(void)
{
    reset_rows(MAX_PROCESS_ROWS);
    for(DWORD i = 0; i < ProcessCount; ++i) {
        DWORD id = ProcessCount - i;
        ProcessList[i].ID = id;
        ProcessList[i].CreationTime = id;
        ProcessList[i].ParentPID = id - 1;
    }
    ULONGLONG started = GetTickCount64();
    FindParentChildProcesses();
    printf("native tree: %lu-row reverse chain constructed in %llu ms\n", ProcessCount,
        (unsigned long long)(GetTickCount64() - started));
    assert(!TreeIndexUnavailable && !TreeUnresolvedParentCount);
    assert(RootProcess.FirstChild == &ProcessList[ProcessCount - 1]);
    for(DWORD i = 0; i < ProcessCount; ++i) {
        process *row = &ProcessList[i];
        assert(row->TreeDepth == ProcessCount - i - 1 && !row->Next);
        assert(row->Parent == (i + 1 < ProcessCount ? &ProcessList[i + 1] : &RootProcess));
        assert(row->FirstChild == (i ? &ProcessList[i - 1] : NULL));
        assert(row->LastChild == row->FirstChild);
    }
}

static void test_shuffled_preorder(void)
{
    static const DWORD ids[] = {3, 1, 8, 5, 2, 7, 6, 4};
    static const DWORD preorder[] = {1, 3, 7, 6, 2, 5, 4, 8};
    static const DWORD depths[] = {0, 1, 2, 2, 1, 2, 2, 3};
    process *flattened = calloc(_countof(ids), sizeof(*flattened));
    assert(flattened);
    reset_rows(_countof(ids));
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].ID = ids[i];
        ProcessList[i].CreationTime = ids[i];
        ProcessList[i].ParentPID = ids[i] / 2;
    }
    FindParentChildProcesses();
    int count = 0;
    ProcessTreeToList(&RootProcess, flattened, &count);
    assert(count == (int)_countof(ids));
    for(int i = 0; i < count; ++i) {
        assert(flattened[i].ID == preorder[i]);
        assert(flattened[i].TreeDepth == depths[i]);
    }
    free(flattened);
}

static void test_uncertain_parent_identity(void)
{
    static const ULONGLONG creation[] = {10, 10, 9, 0, 20, 30, 30, 40};
    static const DWORD parents[] = {0, 1, 1, 1, 4, 6, 99, 1};
    reset_rows(_countof(parents));
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].ID = i + 1;
        ProcessList[i].CreationTime = creation[i];
        ProcessList[i].ParentPID = parents[i];
    }
    FindParentChildProcesses();
    assert(TreeUnresolvedParentCount == 6);
    for(DWORD i = 0; i < 7; ++i) {
        assert(ProcessList[i].Parent == &RootProcess && !ProcessList[i].TreeDepth);
    }
    assert(ProcessList[7].Parent == ProcessList && ProcessList[7].TreeDepth == 1);

    reset_rows(2); /* Strict chronology breaks a malformed cyclic PID graph. */
    ProcessList[0].ID = 1; ProcessList[0].ParentPID = 2; ProcessList[0].CreationTime = 10;
    ProcessList[1].ID = 2; ProcessList[1].ParentPID = 1; ProcessList[1].CreationTime = 20;
    FindParentChildProcesses();
    assert(TreeUnresolvedParentCount == 1);
    assert(ProcessList[0].Parent == &RootProcess && ProcessList[1].Parent == ProcessList);

    reset_rows(3); /* A duplicate parent PID cannot select an arbitrary lifetime. */
    ProcessList[0].ID = 10; ProcessList[0].CreationTime = 1;
    ProcessList[1].ID = 10; ProcessList[1].CreationTime = 2;
    ProcessList[2].ID = 11; ProcessList[2].CreationTime = 3; ProcessList[2].ParentPID = 10;
    FindParentChildProcesses();
    assert(TreeUnresolvedParentCount == 1 && ProcessList[2].Parent == &RootProcess);
}

static void test_index_failure_retains_all_rows(void)
{
    reset_rows(8);
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].ID = i + 1; ProcessList[i].CreationTime = i + 1;
        ProcessList[i].ParentPID = i;
    }
    TreeFailIndexAllocation = TRUE;
    FindParentChildProcesses();
    assert(TreeIndexUnavailable && TreeUnresolvedParentCount == 7);
    assert(_tcsstr(ViMessage, _T("remain visible as roots")));
    process *row = RootProcess.FirstChild;
    for(DWORD i = 0; i < ProcessCount; ++i) {
        assert(row == &ProcessList[i] && !row->TreeDepth && row->Parent == &RootProcess);
        row = row->Next;
    }
    assert(!row && !CollectorError);
    TreeFailIndexAllocation = FALSE;
    FindParentChildProcesses();
    assert(!TreeIndexUnavailable && !TreeUnresolvedParentCount);
    for(DWORD i = 0; i < ProcessCount; ++i) assert(ProcessList[i].TreeDepth == i);
    ProcessCount = 0;
    FindParentChildProcesses();
    assert(!RootProcess.FirstChild && !RootProcess.LastChild && !TreeUnresolvedParentCount);
}

int main(void)
{
    printf("native tree fixture memory: %llu bytes\n", (unsigned long long)MAX_PROCESS_ROWS * sizeof(*ProcessList));
    ProcessList = calloc(MAX_PROCESS_ROWS, sizeof(*ProcessList));
    ViMessage = calloc(DEFAULT_STR_SIZE, sizeof(*ViMessage));
    assert(ProcessList && ViMessage);
    test_wide_tree();
    test_deep_reverse_tree();
    test_shuffled_preorder();
    test_uncertain_parent_identity();
    test_index_failure_retains_all_rows();
    free(ProcessList); ProcessList = NULL;
    free(ViMessage); ViMessage = NULL;
    puts("native tree: bounded construction, lifetime qualification and allocation recovery passed");
    return 0;
}
