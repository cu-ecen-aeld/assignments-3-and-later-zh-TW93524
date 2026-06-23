#!/bin/sh

if [ $# -lt 2 ]; then
    echo "錯誤：請提供兩個參數。用法：$0 <資料夾路徑> <搜尋字串>"
    exit 1
fi

filesdir=$1
searchstr=$2


if [ ! -d "$filesdir" ]; then
    echo "錯誤：'$filesdir' 不是一個有效的資料夾路徑！"
    exit 1
fi


file_count=$(find "$filesdir" -type f | wc -l)


match_count=$(grep -r "$searchstr" "$filesdir" | wc -l)


echo "The number of files are $file_count and the number of matching lines are $match_count"
