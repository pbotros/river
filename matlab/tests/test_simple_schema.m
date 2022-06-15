
% col1 is an int32, but col2 is "FIXED_WIDTH_BYTES", i.e. contains 10
% bytes of data. In MATLAB, this is represented by an array of uint8 of
% length 10.
schema = StreamSchema({'col1', 'col2'}, {'int32', 'cell'}, [0, 10]);

written_data = schema.new_table(4);
written_data{:, 'col1'} = [1, 2, 4, 6]';

% Fill with random data
for i = 1:4
    written_data{i, 'col2'} = {randi(100, 1, 10, "uint8")};
end

test_read_write(schema, written_data);