classdef RedisConnection < handle
    properties (Access = public, Hidden = true)
        mexInterface;
    end
    methods
        function this = RedisConnection(stream_name, port, varargin)
            dir = fileparts(mfilename('fullpath'));
            this.mexInterface = MexInterface(...
                'RedisConnection', ...
                str2fun([dir '/redis_connection_mex']), ...
                stream_name, port, varargin{:}); 
        end
        
        function out = redis_hostname(this)
            out = this.mexInterface.call_method('redis_hostname');
        end
        function out = redis_port(this)
            out = this.mexInterface.call_method('redis_port');
        end
        function out = redis_password(this)
            out = this.mexInterface.call_method('redis_password');
        end
    end
end
