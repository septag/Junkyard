@echo off

pushd %dp~0
python amalgamate-core.py --rootdir ../../code/Core --outputname Core --ignore-comment-lines --outputdir %1
popd