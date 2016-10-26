#!/bin/bash
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

#set -xv

function SegvToolUsage()
{
	echo "Usage:" 
	echo "$0 backtrace [path/deal_segv.log]"
}

#格式化输出空格
function PrintBlank()
{
	myloopcount=$1
	for i in $(seq 1 $myloopcount)
	do
		printf " "
	done
}

function BackTrace()
{
	logfile="/data/log/deal_segv.log"

	if [ $# -eq 1 ] ; then
		logfile="$1"
	fi
	
	if [ ! -s $logfile ] ; then
		echo "File $logfile not exist."
		exit;
	fi

	#获取最后一次coredump的log的开始行号
	linenum=$(cat -n ${logfile} | sed -n '/SEGV: Meet SIG:/p' | tail -1 | awk '{print $1}')
	#获取最后一次coredump的内容，并输出
	logcontext=$(awk -v startnum="${linenum}" '{if(NR >= startnum) print $0}' ${logfile})
	echo "Read CoreDump Log:"
	echo "========================================="
	echo "$logcontext"
	echo "========================================="
	echo ""

	#获取coredump时的IP寄存器值
	mycoredumpip=$(echo "$logcontext" | 
	grep -o "SEGV: Meet SIG:[0-9]\{1,4\} at 0x[0-9,a-f,A-F]\{1,4\}:0x[0-9,a-f,A-F]\{1,24\}" | 
	awk -F"at" '{ print $2}' | 
	awk -F":" '{print $2}' | awk -F"," '{print $1}')

	#获取所有内存映射
	procmap=$(echo "$logcontext" | sed -n '/Memory map Start/,/Memory map End/p' | sed '1d' | sed '$d')

	#获取backtrace相关内容
	backtracenum=$(echo "$logcontext" | grep -o "SEGV: Obtained [0-9]\{1,3\} stack frames:" | grep -o "[0-9]\{1,3\}") 
	backtracecontext=$(echo "$logcontext" | grep "SEGV: Obtained [0-9]\{1,3\} stack frames:" -A ${backtracenum} | sed '1d' )

	echo "Coredump at:"
	echo "$procmap" | while read line2
	do
		result=$(echo "$line2" | awk -F"-" -v addr="$mycoredumpip" '{$1="0x"$1;$1=strtonum($1);$2="0x"$2;$2=strtonum($2);if(strtonum(addr)>=$1 && strtonum(addr)<=$2){print "FindAddr"}}')
		#找到了coredump时的ip寄存器值所在的内存映射
		if [ "$result"x = "FindAddr"x ] ; then
			#获取进程文件名 有可能为业务进程、系统lib（/lib64/librt-2.12.so等）
			myexefile=$(echo "$line2" | awk '{for(i=1;i<=NF;i++){if($i ~ /^\//) print $i}}')
			if [ "$myexefile"x = ""x ] ; then
				printf "[%s]\n" $mycoredumpip
				break;
			fi

			#查看进程是否为so
			issofile=$(file $myexefile | grep "shared object")
			if [ -n "$issofile" ]; then
				sofilestartaddr=$(echo "$line2" | awk -F"-" '{$1="0x"$1;printf("0x%x", strtonum($1))}')
				let mycoredumpip=$mycoredumpip-$sofilestartaddr
			fi
			mycoredumpip=$(printf "0x%x" $mycoredumpip)

			#使用addr2line找到该coredump时的ip寄存器所存地址在进程中对应的代码是什么
			addr2lineresult=$(addr2line -f -C -e $myexefile $mycoredumpip)
			printf "["
			printf "%s" "$(echo "$addr2lineresult" | sed -n 1p)"
			printf "] "
			funcpath=$(echo "$addr2lineresult" | sed -n 2p)
			if [ "$funcpath"x = "??:0"x ]; then
				printf "%s" "$myexefile"
				isnotstrip=$(file $myexefile | grep -o "not stripped")
				if [ "$isnotstrip"x != "not stripped"x ]; then
					printf " [stripped]"
				fi
			else
				printf "%s" "$funcpath"
			fi
			if [ -n "$issofile" ]; then
				printf " [0x%x]" ${mycoredumpip}
			fi
			printf "\n\n"
			break;
		fi
	done

	echo "Backtrace:"
	echo "$backtracecontext" | while read line1
	do
		#backtrace地址
		backaddr=$(echo ${line1} | awk -F":" '{print $2}')
		lineaddr=$(echo ${line1} | awk -F"[" '{print $2}' | awk -F"]" '{print $1}')
		echo "$procmap" | while read line2
		do
			#找bacetrace的地址 的 内存映射
			result=$(echo "$line2" | awk -F"-" -v addr="$backaddr" '{$1="0x"$1;$1=strtonum($1);$2="0x"$2;$2=strtonum($2);if(strtonum(addr)>=$1 && strtonum(addr)<=$2){print "FindAddr"}}')
			if [ "$result"x = "FindAddr"x ] ; then
				myexefile=$(echo "$line2" | awk '{for(i=1;i<=NF;i++){if($i ~ /^\//) print $i}}')

				if [ $lineaddr -lt 3 ] ; then
					continue;
				fi

				if [ "$myexefile"x = ""x ] ; then
					PrintBlank ${lineaddr}
					printf "<== "
					printf "[%s]\n" $backaddr
					break;
				fi

				if [ ! $(($mycoredumpip)) -eq $(($backaddr)) ] ; then
					let backaddr=$backaddr-0x1
				fi
				issofile=$(file $myexefile | grep "shared object")
				if [ -n "$issofile" ]; then
					sofilestartaddr=$(echo "$line2" | awk -F"-" '{$1="0x"$1;printf("0x%x", strtonum($1))}')
					let backaddr=$backaddr-$sofilestartaddr
				fi
				backaddr=$(printf "0x%x" $backaddr)
				addr2lineresult=$(addr2line -f -C -e $myexefile $backaddr)
				PrintBlank ${lineaddr}
				printf "<== "
				printf "["
				printf "%s" "$(echo "$addr2lineresult" | sed -n 1p)"
				printf "] "
				funcpath=$(echo "$addr2lineresult" | sed -n 2p)
				if [ "$funcpath"x = "??:0"x ]; then
					printf "%s" "$myexefile"
					isnotstrip=$(file $myexefile | grep -o "not stripped")
					if [ "$isnotstrip"x != "not stripped"x ]; then
						printf " [stripped]"
					fi
				else
					printf "%s" "$funcpath"
				fi
				if [ -n "$issofile" ]; then
					printf " [0x%x]" ${backaddr}
				fi
				printf "\n"
			fi
		done
	done
}

case $1 in
	backtrace|bt)
	if [ $# -eq 1 ] ; then
		BackTrace
	elif [ $# -eq 2 ] ; then
		BackTrace $2
	else
		SegvToolUsage
	fi
	;;
	*)
	SegvToolUsage
	exit;
esac
