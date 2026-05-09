struct log_request {
    string username<256>;
    string operation<256>;
    string filename<256>;
};

program LOG_PROG {
    version LOG_VERS {
        int PRINT_LOG(log_request) = 1;
    } = 1;
} = 0x20000001;