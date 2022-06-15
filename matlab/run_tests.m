%%
% Connect to localhost's Redis
c = RedisConnection('127.0.0.1', 6379);

% Simple schema with two columns
schema = StreamSchema({'col1', 'col2'}, {'int32', 'double'});

% Initialize the stream for writing:
w = StreamWriter(c);
w.initialize(char(matlab.lang.internal.uuid()), schema);
stream_name = w.stream_name();
disp(stream_name);

% Create some data to write
written_data = w.new_table(4);
written_data{:, 'col1'} = [1, 2, 4, 6]';
written_data{:, 'col2'} = [1.1, -10.5, -100.46, 1931]';

% And write it!
w.write_table(written_data);
w.stop();

%%
% Now setup the reader pointing to the just-created stream:
c = RedisConnection('127.0.0.1', 6379);
stream_name = '41782702-d686-4351-aa84-f292892b7ee5';


r = StreamReader(c);
r.initialize(stream_name);
disp(r.stream_name());
disp(r.schema_field_names());

% And read!
[n_read, read_data] = r.read_struct_array(10);
r.stop();

disp(read_data);

%%
% Check the data matches
assert(height(read_data) == height(written_data));
assert(width(read_data) == width(written_data));
for row_idx = 1:height(read_data)
    for col_idx = 1:width(written_data)
        assert(read_data{row_idx, col_idx} == written_data{row_idx, col_idx});
    end
end
