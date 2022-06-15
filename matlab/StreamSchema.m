classdef StreamSchema < handle
    properties (Access = public, Hidden = true)
        mexInterface;
    end
    methods
        function this = StreamSchema(col_names, col_types, varargin)
            dir = fileparts(mfilename('fullpath'));
            this.mexInterface = MexInterface(...
                'StreamSchema', ...
                str2fun([dir '/stream_schema_mex']), ...
                col_names, col_types, varargin{:}); 
        end
        
        function out = field_names(this)
            out = this.mexInterface.call_method('field_names');
        end

        function out = field_types(this)
            out = this.mexInterface.call_method('field_types');
        end

        function out = field_sizes(this)
            out = this.mexInterface.call_method('field_sizes');
        end
        
        function out = new_table(this, n)
            field_names = this.field_names();
            field_types = this.field_types();
            out = table(...
                'Size', [n, length(field_names)], ...
                'VariableTypes', field_types, ...
                'VariableNames', field_names);
        end
    end
end
