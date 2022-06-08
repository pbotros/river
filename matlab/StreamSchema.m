classdef StreamSchema < handle
    properties (Access = public, Hidden = true)
        mexInterface;
    end
    methods
        function this = StreamSchema(col_names, col_types)
            dir = fileparts(mfilename('fullpath'));
            this.mexInterface = MexInterface(...
                'StreamSchema', ...
                str2fun([dir '/stream_schema_mex']), ...
                col_names, col_types); 
        end
        
        function out = field_names(this)
            out = this.mexInterface.call_method('field_names');
        end

        function out = field_types(this)
            out = this.mexInterface.call_method('field_types');
        end
    end
end
