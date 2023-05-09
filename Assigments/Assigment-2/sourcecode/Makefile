compile:
	gcc -o program hw2.c

test:
	echo 'this is test1' > test1.txt
	echo 'this is test2 and second test file' > test2.txt
	echo 'this is test3' > test3.txt
	echo 'this is test4, my name is Suleyman' > test4.txt
	echo 'this is test5, you can see this test sentences in this file' > test5.txt
	echo 'empty' > test6.txt
	echo "test1\ntest2\ntes3\ntestt4\nt5" > file.txt

run:
	./program

runValgrind:
	valgrind -s ./program

clean:
	rm *.txt program