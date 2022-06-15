
% Simple schema with two columns
schema = StreamSchema({'col1', 'col2'}, {'int32', 'double'});

% Create some data to write
written_data = schema.new_table(4);
written_data{:, 'col1'} = [1, 2, 4, 6]';
written_data{:, 'col2'} = [1.0, -10.0, -100.0, 1931]';

test_read_write(schema, written_data);