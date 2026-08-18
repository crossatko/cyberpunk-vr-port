#!/usr/bin/env bash
# RED4ext.SDK fixes needed to compile with clang-cl on Linux. Re-run after any
# submodule update. Idempotent.
set -euo pipefail
R="$(dirname "$0")/../../externals/RED4ext.SDK/include/RED4ext"
# 1) case-sensitive FS: sources include RED4ext/rtti/, the directory is RTTI/
[ -e "$R/rtti" ] || ln -s RTTI "$R/rtti"
# 2) Handle<T> instantiated on forward-declared types: clang instantiates the dtor's
#    static_assert where MSVC does not, so pull in the real definitions.
python3 - "$R" <<'PY'
import os,sys
R=sys.argv[1]
need={
 "Scripting/Natives/Generated/anim/PoseLink.hpp":                   ["RED4ext/Scripting/Natives/Generated/anim/AnimNode_Base.hpp"],
 "Scripting/Natives/Generated/anim/AnimGraph.hpp":                  ["RED4ext/Scripting/Natives/Generated/anim/AnimNode_Root.hpp"],
 "Scripting/Natives/Generated/anim/AnimNode_Container.hpp":         ["RED4ext/Scripting/Natives/Generated/anim/AnimNode_Base.hpp"],
 "Scripting/Natives/Generated/anim/AnimDatabaseCollectionEntry.hpp":["RED4ext/Scripting/Natives/Generated/anim/GenericAnimDatabase.hpp",
                                                                     "RED4ext/Scripting/Natives/Generated/C2dArray.hpp"],
 "Scripting/Natives/Generated/anim/RigSharedData.hpp":              ["RED4ext/Scripting/Natives/Generated/anim/IRigIkSetup.hpp"],
 "Scripting/Natives/Generated/anim/Rig.hpp":                        ["RED4ext/Scripting/Natives/Generated/anim/IRigIkSetup.hpp"],
 "Scripting/Natives/animRig.hpp":                                   ["RED4ext/Scripting/Natives/Generated/anim/IRigIkSetup.hpp"],
}
for rel,incs in need.items():
    p=os.path.join(R,rel)
    if not os.path.isfile(p): continue
    s=open(p,encoding='utf-8',errors='replace').read()
    add=[i for i in incs if i not in s]
    if not add: continue
    L=s.split("\n"); last=max(n for n,l in enumerate(L) if l.startswith("#include"))
    L[last+1:last+1]=[f'#include <{i}>' for i in add]
    open(p,"w",encoding='utf-8').write("\n".join(L))
    print(f"  patched {rel}")
PY
echo "SDK clang fixes applied."
