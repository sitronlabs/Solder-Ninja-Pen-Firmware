Import("env")

from datetime import datetime, timezone
import os
import re
import subprocess

# Save time
version_time = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

# Retrieve version information from git
git_branch = subprocess.check_output("git rev-parse --abbrev-ref HEAD", shell=True).decode().strip()
git_describe = subprocess.check_output("git describe --always --tags --long --dirty", shell=True).decode().strip()
match = re.match('^(v([0-9]*?)\.([0-9]*?)\.([0-9]*?)-([0-9]*?)-g)?([0-9A-Fa-f]{5,40})(-dirty)?$', git_describe)
if match:
	version_major = match.group(2) or "0"
	version_minor = match.group(3) or "0"
	version_patch = match.group(4) or "0"
	git_post = match.group(5) or "0"
	git_hash = match.group(6)
	git_dirty = "dirty" in (match.group(7) or "")
	version_string = "v" + version_major + "." + version_minor + "." + version_patch
	if int(git_post) > 0:
		version_string += "-post." + git_post
	version_string += "+" + version_time
	version_string += ".git" + git_hash + "." + git_branch
	if git_dirty:
		version_string += ".dirty"
else:
	version_major = "0"
	version_minor = "0"
	version_patch = "0"
	version_string = "v" + version_major + "." + version_minor + "." + version_patch
	version_string += "+" + version_time

# Display version
print("Building version " + version_string)

# Output version to header file
if not os.path.exists("gen"):
    os.makedirs("gen")
f = open("gen/version.h", "w")
f.write("#ifndef VERSION_H\n#define VERSION_H\n")
f.write("const int version_major = " + version_major + ";\n")
f.write("const int version_minor = " + version_minor + ";\n")
f.write("const int version_patch = " + version_patch + ";\n")
f.write("const char version_date[] = \"" + version_time + "\";\n")
f.write("const char version_commit[] = \"" + git_hash + "\";\n")
f.write("const char version_string[] = \"" + version_string + "\";\n")
f.write("#endif\n")

# Rename output file
env.Replace(PROGNAME="slto00001-%s" % version_string)
