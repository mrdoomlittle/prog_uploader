# include <boost/asio/serial_port.hpp>
# include <boost/asio.hpp>
# include <cstdio>
# include <eint_t.hpp>
# include <iostream>
# include <sys/stat.h>
# include <string.h>
# include <fcntl.h>
# include <errno.h>
# include <stdint.h>
# include <stdlib.h>
# include <unistd.h>
# include <time.h>

mdl::u8_t put_byte = 0x1;
mdl::u8_t get_byte = 0x2;
mdl::u8_t page_update = 0x3;
mdl::u8_t reset_addr = 0x4;
mdl::u8_t exec_code = 0x5;

void prog_send_instr(boost::asio::serial_port *__serial_port, mdl::u8_t __i) {
	for (;;) {
		boost::asio::write(*__serial_port, boost::asio::buffer(&__i, sizeof(mdl::u8_t)));
		if (::tcdrain(__serial_port->native_handle()) < 0)
			fprintf(stderr, "prog_uploader, failed to write.\n");

		mdl::u8_t ack;
		_redo:
		try {
			boost::asio::read(*__serial_port, boost::asio::buffer(&ack, sizeof(mdl::u8_t)));
		} catch(boost::system::system_error const& e) {goto _redo;}

		if (ack != 1)
			fprintf(stderr, "prog_uploade, sync error.\n");
		else break;
	}
}

mdl::i8_t write_data(mdl::u8_t *__bin, mdl::uint_t __bc, boost::asio::serial_port *__serial_port) {
	prog_send_instr(__serial_port, reset_addr);

	printf("prog_uploader, writing program data.\n");
	mdl::u8_t ic = 0;
	for (mdl::u8_t *itr = __bin; itr != __bin+__bc; itr++) {
		prog_send_instr(__serial_port, put_byte);
		if (!ic) {
			printf("prog_uploader, sent; [%x] . ", *itr);
			ic++;
		} else {
			if (ic != 64 && itr != __bin+__bc-1) {
				printf("[%x] . ", *itr);
				ic++;
			} else {
				printf("[%x]\n\n", *itr);
				ic = 0;
			}
		}

		boost::asio::write(*__serial_port, boost::asio::buffer(itr, sizeof(mdl::u8_t)));
		::tcdrain(__serial_port->native_handle());
	}
	return 0;
}

mdl::i8_t validate_data(mdl::u8_t *__bin, mdl::uint_t __bc, boost::asio::serial_port *__serial_port) {
	prog_send_instr(__serial_port, reset_addr);
	printf("prog_uploader, valadating program data. please wait...\n");
	float rate = 10.0f;
	float present_done = 0.0f;
	float one_present = 100.0f/(float)__bc;
	for (mdl::u8_t *itr = __bin; itr != __bin+__bc; itr++) {
		if (round(present_done/rate) > round((present_done-one_present)/rate)) {
			printf("prog_uploader, %f present finished.\n", present_done);
		}

		mdl::u8_t inbound_byte = 0;
		prog_send_instr(__serial_port, get_byte);

		_redo:
		try {
			boost::asio::read(*__serial_port, boost::asio::buffer(&inbound_byte, sizeof(mdl::u8_t)));
		} catch(boost::system::system_error const& e) {goto _redo;}

		if (*itr != inbound_byte) {
			fprintf(stderr, "error, byte mismatch.\n");
			fprintf(stderr, "got: %x not %x, off: %u\n", inbound_byte, *itr, itr-__bin);
			return -1;
		}

		present_done+=one_present;
	}
	return 0;
}

mdl::i8_t upload_program(mdl::u8_t *__bin, mdl::uint_t __bc, boost::asio::serial_port *__serial_port) {
	if (write_data(__bin, __bc, __serial_port) < 0) return -1;
	prog_send_instr(__serial_port, page_update);
	if (validate_data(__bin, __bc, __serial_port) < 0) return -1;
	return 0;
}

int main(int argc, char const *argv[]) {
	if (argc < 3) {
		printf("usage: prog_uploader [binary file] [serial port].\n");
		return -1;
	}

	mdl::u8_t *bin = nullptr;
	char const *fpth = argv[1];

	int fd;
	if ((fd = open(fpth, O_RDONLY)) < 0) {
		fprintf(stderr, "prog_uploader, failed to open file at: %s\n", fpth);
		return -1;
	}

	struct stat st;
	if (stat(fpth, &st) < 0) {
		fprintf(stderr, "prog_uploader, failed to stat file at: %s\n", fpth);
		close(fd);
		return -1;
	}

	bin = (mdl::u8_t*)malloc(st.st_size);
	read(fd, bin, st.st_size);
	printf("prog_uploader, loaded binary file with the size of %u bytes.\n", st.st_size);

	boost::asio::serial_port_base::character_size chr_size(8);
	boost::asio::io_service io_service;
	boost::asio::serial_port serial_port(io_service, argv[2]);
	serial_port.set_option(boost::asio::serial_port_base::baud_rate(38400));
	serial_port.set_option(chr_size);

	printf("prog_uploader, waiting for serial port to open, please wait...\n");
	while(!serial_port.is_open()){};
	printf("prog_uploader, serial port is now open.\n");


	printf("prog_uploader, please wait for 8seconds for the uploader to setup.\n");
	usleep(8000000); // wait
	printf("prog_uploader, thanks for waiting, please input a command.\n");

	char instr_buff[128];
	while(1) {
		printf("prog_uploader: ");
		scanf("%s", instr_buff);

		if (!strcmp(instr_buff, "help")) {
			printf("commands: [help, upload, exec, (exit, quit)].\n");
		} else if (!strcmp(instr_buff, "upload")) {

			printf("uploader program data to device.\n");
			if (upload_program(bin, st.st_size, &serial_port) < 0) {
				fprintf(stderr, "upload failed.\n");
				break;
			}

			printf("program data successfully uploaded.\n");

		} else if (!strcmp(instr_buff, "validate")) {
			validate_data(bin, st.st_size, &serial_port);
		} else if (!strcmp(instr_buff, "reload")) {
			if (stat(fpth, &st) < 0) {
				fprintf(stderr, "prog_uploader, failed to stat file: '%s'\n", fpth);
				goto _err;
			}

			free(bin);
			bin = (mdl::u8_t*)malloc(st.st_size);
			lseek(fd, 0, SEEK_SET);
			if (read(fd, bin, st.st_size) < 0) {
				fprintf(stderr, "prog_uploader, failed to read file.\n");
			}

			printf("prog_uploader, loaded binary file with the size of %u bytes. %u\n", st.st_size, *bin);
		} else if (!strcmp(instr_buff, "exec")) {
			struct timespec begin, end;
			clock_gettime(CLOCK_MONOTONIC, &begin);
			prog_send_instr(&serial_port, exec_code);
			mdl_u8_t ack;
			boost::asio::read(serial_port, boost::asio::buffer(&ack, sizeof(mdl::u8_t)));
			clock_gettime(CLOCK_MONOTONIC, &end);
			printf("execution time: %luns\n", (end.tv_nsec-begin.tv_nsec)+((end.tv_sec-begin.tv_sec)*1000000000));
		} else if (!strcmp(instr_buff, "exit") || !strcmp(instr_buff, "quit")) {
			printf("goodbyte.\n");
			break;
		}
	}

	_err:
	close(fd);
	serial_port.close();
	free(bin);
}
