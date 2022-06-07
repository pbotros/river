%STR2FUN Construct a function_handle from a function name or path.
%    FUNHANDLE = STR2FUN(S) constructs a function_handle FUNHANDLE to the
%    function named in the character vector S. The S input must be a
%    character vector.  The S input cannot be a character array with
%    multiple rows or a cell array of character vectors.
%
%    You can create a function handle using either the @function syntax or
%    the STR2FUN command. You can create an array of function handles from
%    character vectors by creating the handles individually with STR2FUN,
%    and then storing these handles in a cell array.
%
%    Examples:
%
%      To create a function handle from the function name, 'humps':
%
%        fhandle = str2func('humps')
%        fhandle = 
%            @humps
%
%      To call STR2FUNC on a cell array of character vectors, use the
%      CELLFUN function. This returns a cell array of function handles:
%
%        fh_array = cellfun(@str2func, {'sin' 'cos' 'tan'}, ...
%                           'UniformOutput', false);
%        fh_array{2}(5)
%        ans =
%           0.2837
%
%    See also STR2FUNC, FUNCTION_HANDLE, FUNC2STR, FUNCTIONS.

function fun = str2fun(str)
assert(ischar(str));
if str(1) ~= '@'
    [p, str] = fileparts(str);
    if ~isempty(p)
        cwd = cd(p);
        cleanup_obj = onCleanup(@() cd(cwd));
    end
end
fun = str2func(str);
end
