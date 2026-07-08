#pragma once
#include <string>
#include <filesystem>








namespace PathUtils
{
    
    
    
    const std::filesystem::path& GetExecutableDir();

    
    
    
    std::filesystem::path FindAncestorContaining(const std::filesystem::path& start,
                                                 const std::string& marker);

    
    
    
    
    
    
    
    
    
    
    
    
    std::filesystem::path ResolveAsset(const std::string& relativeToAssets,
                                       const std::filesystem::path& preferredRoot = {});
}
