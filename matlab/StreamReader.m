classdef StreamReader < handle
    properties (Access = public, Hidden = true)
        mexInterface;
        m_schema;
    end
    
    methods
        function this = StreamReader(redis_connection)
            dir = fileparts(mfilename('fullpath'));
            this.mexInterface = MexInterface(...
                'StreamReader', ...
                str2fun([dir '/stream_reader_mex']), ...
                redis_connection.mexInterface); 
            this.m_schema = [];
        end
        
        function initialize(this, stream_name)
            this.mexInterface.call_method('initialize', stream_name);

            % Cache schema since it will be called on each `read`
            this.populate_schema();
        end
        
        function out = stream_name(this)
            out = this.mexInterface.call_method('stream_name');
        end

        function stop(this)
            this.mexInterface.call_method('stop');
        end

        function out = schema_field_names(this)
            this.populate_schema();
            out = this.m_schema.field_names();
        end

        function out = schema_field_types(this)
            this.populate_schema();
            out = this.m_schema.field_types();
        end

        function out = new_table(this, n)
            this.populate_schema();
            out = this.m_schema.new_table(n);
        end
        
        function output_table = read_table(this, n)
            [n_read, data] = this.mexInterface.call_method('read', n);
            field_names = this.schema_field_names();
            field_types = this.schema_field_types();
            n_cols = length(field_names);
            if n_read >= 0
                output_table = table(...
                    'Size', [n_read, n_cols], ...
                    'VariableTypes', field_types, ...
                    'VariableNames', field_names);
                for col_idx = 1:n_cols
                    output_table{:, col_idx} = data{col_idx};
                end
            else
                output_table = table(...
                    'Size', [0, n_cols], ...
                    'VariableTypes', field_types, ...
                    'VariableNames', field_names);
            end
        end
    end
    methods (Access = private)
        function populate_schema(this)
            if ~isempty(this.m_schema)
                return
            end
            % Cache
            field_names = this.mexInterface.call_method('schema_field_names');
            field_types = this.mexInterface.call_method('schema_field_types');
            field_sizes = this.mexInterface.call_method('schema_field_sizes');
            this.m_schema = StreamSchema(field_names, field_types, field_sizes);
        end
    end
end
