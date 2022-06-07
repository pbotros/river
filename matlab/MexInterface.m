% MEX_INTERFACE MATLAB wrapper to an underlying C++ class
% Based on, but modified, from https://www.mathworks.com/matlabcentral/fileexchange/38964-example-matlab-class-wrapper-for-a-c-class
% NOTE: In MATLAB 2019a, some functionality was introduced to
% call C++ libraries directly from MATLAB. To maximize compatibility
% for groups that might be using legacy MATLAB, the "traditional"
% MEX/C route was taken.
classdef MexInterface < handle
    properties (Access = private, Hidden = true)
        className; % string for description
        mexHandle; % Handle to the mex function
    end
    properties (Access = public)
        objectHandle; % Handle to the underlying C++ class instance
    end
    methods
        function this = MexInterface(className, mexfun, varargin)
            this.className = className;
            this.mexHandle = mexfun;
            this.objectHandle = this.mexHandle('new', varargin{:});
        end

        %% Destructor - Destroy the C++ class instance
        function delete(this)
            if ~isempty(this.objectHandle)
                this.mexHandle('delete', this.objectHandle);
            end
            this.objectHandle = [];
        end

        function ret = ptr(this)
            ret = this.objectHandle;
        end

        function disp(this, var_name)
            fprintf('%s\n', this.className);
        end
%
        function varargout = call_method(this, method_name, varargin)
            [varargout{1:nargout}] = this.mexHandle(method_name, this.objectHandle, varargin{:});
        end
    end
end
