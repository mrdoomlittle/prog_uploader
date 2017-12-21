all: clean
	bcc -i main.bc -o main.rbc -I /home/daniel-robson/Projects/bcc/lib
clean:
	> main.rbc
