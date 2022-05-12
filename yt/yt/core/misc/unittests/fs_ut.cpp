#include "gtest/gtest.h"
#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/core/misc/fs.h>

#include <util/folder/dirut.h>

namespace NYT::NFS {
namespace {

////////////////////////////////////////////////////////////////////////////////

TEST(TFSTest, TestGetRealPath)
{
    auto cwd = NFs::CurrentWorkingDirectory();
    EXPECT_EQ(CombinePaths(cwd, "dir"), GetRealPath("dir"));
    EXPECT_EQ(cwd, GetRealPath("dir/.."));
    EXPECT_EQ(CombinePaths(cwd, "dir"), GetRealPath("dir/./a/b/../../."));
    EXPECT_EQ(GetRealPath("/a"), "/a");
    EXPECT_EQ(GetRealPath("/a/b"), "/a/b");
    EXPECT_EQ(GetRealPath("/a/b/c/.././../d/."), "/a/d");
}

TEST(TFSTest, TestGetDirectoryName)
{
    auto cwd = NFs::CurrentWorkingDirectory();
    EXPECT_EQ(GetDirectoryName("/a/b/c"), "/a/b");
    EXPECT_EQ(GetDirectoryName("a/b/c"), cwd + "/a/b");
    EXPECT_EQ(GetDirectoryName("."), cwd);
    EXPECT_EQ(GetDirectoryName("/"), "/");
    EXPECT_EQ(GetDirectoryName("/a"), "/");
}

TEST(TFSTest, TestIsDirEmtpy)
{
    auto cwd = NFs::CurrentWorkingDirectory();
    auto dir = CombinePaths(cwd, "test");
    MakeDirRecursive(dir);
    EXPECT_TRUE(IsDirEmpty(dir));
    MakeDirRecursive(CombinePaths(dir, "nested"));
    EXPECT_FALSE(IsDirEmpty(dir));
    RemoveRecursive(dir);
}

TEST(TFSTest, TestIsPathRelativeAndInvolvesNoTraversal)
{
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal(""));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("some"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("some/file"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("."));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("./some/file"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("./some/./file"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("./some/.."));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("a/../b"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("a/./../b"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("some/"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("some//"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("a//b"));
    EXPECT_TRUE(NFS::IsPathRelativeAndInvolvesNoTraversal("a/b/"));

    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal("/"));
    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal("//"));
    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal("/some"));
    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal(".."));
    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal("../some"));
    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal("a/../.."));
    EXPECT_FALSE(NFS::IsPathRelativeAndInvolvesNoTraversal("a/../../b"));
}

TEST(TFSTest, TestGetRelativePath)
{
    EXPECT_EQ(GetRelativePath("/a", "/a/b"), "b");
    EXPECT_EQ(GetRelativePath("/a/b", "/a"), "..");
    EXPECT_EQ(GetRelativePath("/a/b/c", "/d/e"), "../../../d/e");
    EXPECT_EQ(GetRelativePath("/a/b/c/d", "/a/b"), "../..");
    EXPECT_EQ(GetRelativePath("/a/b/c/d/e", "/a/b/c/f/g/h"), "../../f/g/h");
    EXPECT_EQ(GetRelativePath("a/b/c", "d/e"), "../../../d/e");
    EXPECT_EQ(GetRelativePath("/a/b", "/a/b"), ".");

    EXPECT_EQ(GetRelativePath(CombinePaths(NFs::CurrentWorkingDirectory(), "dir")), "dir");
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NFS

