mtn_setup()

addfile("somefile", "grrr\n")
addfile("testfile1", "blah blah\n")
mkdir("testdir")
addfile("testdir/testfile2", "stuff stuff\n")
commit()

writefile("somefile", "GRRR\n")
remove("testfile1")
remove("testdir/testfile2")
chdir("testdir")
check(mtn("ls", "missing"), 0, true, false)
check(samelines("stdout", {"testdir/testfile2", "testfile1"}))

check(mtn("revert", "--missing", "."), 0, false, false)
check(not exists("../testfile1"))
check(exists("testfile2"))
check(readfile("../somefile") == "GRRR\n")

remove("testfile2")
check(mtn("revert", "--missing", ".."), 0, false, false)
check(exists("../testfile1"))
check(exists("testfile2"))
check(readfile("../somefile") == "GRRR\n")

remove("testfile2")
remove("../testfile1")
check(mtn("revert", "--missing"), 0, false, false)
check(exists("../testfile1"))
check(exists("testfile2"))
check(readfile("../somefile") == "GRRR\n")

check(mtn("revert", ".."), 0, false, false)
check(exists("../testfile1"))
check(exists("testfile2"))
check(readfile("../somefile") == "grrr\n")