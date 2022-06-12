classdef StreamWriter < handle
    properties (Access = public, Hidden = true)
        mexInterface;
        m_schema;
    end
    
    methods
        function this = StreamWriter(redis_connection)
            dir = fileparts(mfilename('fullpath'));
            this.mexInterface = MexInterface(...
                'StreamWriter', ...
                str2fun([dir '/stream_writer_mex']), ...
                redis_connection.mexInterface); 
            this.m_schema = [];
        end
        
        function initialize(this, stream_name, schema)
            this.mexInterface.call_method(...
                'initialize',...
                stream_name, ...
                schema.mexInterface);
            
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
            field_names = this.schema_field_names();
            field_types = this.schema_field_types();
            out = table(...
                'Size', [n, length(field_names)], ...
                'VariableTypes', field_types, ...
                'VariableNames', field_names);
        end
        
        function write_table(this, input_table)
            % Convert to cell array
            field_names = this.schema_field_names();
            n_cols = length(field_names);
            
            input_data = {};
            for col_idx = 1:n_cols
                input_data{col_idx} = input_table.(field_names{col_idx});
            end
            this.mexInterface.call_method('write', input_data);
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
            this.m_schema = StreamSchema(field_names, field_types);
        end
    end
end
