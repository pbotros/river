function test_read_write(schema, written_data)
% Connect to localhost's Redis
c = RedisConnection('127.0.0.1', 6379);

% Initialize the stream for writing:
w = StreamWriter(c);
w.initialize(char(matlab.lang.internal.uuid()), schema);
stream_name = w.stream_name();

% And write it!
w.write_table(written_data);
w.stop();

% Now setup the reader pointing to the just-created stream:
r = StreamReader(c);
r.initialize(stream_name);
read_data = r.read_table(height(written_data));
r.stop();

% Check the data matches
assert(height(read_data) == height(written_data));
assert(width(read_data) == width(written_data));
for row_idx = 1:height(read_data)
    for col_idx = 1:width(written_data)
        if isa(read_data{row_idx, col_idx}, 'cell')
            read_data_cell = read_data{row_idx, col_idx};
            written_data_cell = written_data{row_idx, col_idx};
            assert(all(read_data_cell{1} == written_data_cell{1}));
        else
            assert(read_data{row_idx, col_idx} == written_data{row_idx, col_idx});
        end
        
    end
end
end
