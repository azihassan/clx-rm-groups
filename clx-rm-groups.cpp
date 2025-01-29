#include <cassert>
#include <numeric>
#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <functional>
#include <cstdint>

uint16_t readLittleEndianWord(std::ifstream& stream)
{
    uint8_t bytes[2] = {0};
    stream.read(reinterpret_cast<char*>(&bytes[1]), sizeof(uint8_t));
    stream.read(reinterpret_cast<char*>(&bytes[0]), sizeof(uint8_t));
    return bytes[1] | (bytes[0] << 8);
}

uint32_t readLittleEndianDoubleWord(std::ifstream& stream)
{
    uint8_t bytes[4] = {0};
    stream.read(reinterpret_cast<char*>(&bytes[3]), sizeof(uint8_t));
    stream.read(reinterpret_cast<char*>(&bytes[2]), sizeof(uint8_t));
    stream.read(reinterpret_cast<char*>(&bytes[1]), sizeof(uint8_t));
    stream.read(reinterpret_cast<char*>(&bytes[0]), sizeof(uint8_t));
    return bytes[3] | bytes[2] << 8 | bytes[1] << 16 | bytes[0] << 24;
}

void writeLittleEndianWord(std::ofstream& stream, uint16_t value)
{
    for(int i = 0; i < 2; i++)
    {
        uint8_t byte = value & 0xff;
        stream.write(reinterpret_cast<char*>(&byte), sizeof(uint8_t));
        value >>= 8;
    }
}

void writeLittleEndianDoubleWord(std::ofstream& stream, uint32_t value)
{
    for(int i = 0; i < 4; i++)
    {
        uint8_t byte = value & 0xff;
        stream.write(reinterpret_cast<char*>(&byte), sizeof(uint8_t));
        value >>= 8;
    }
}

struct Frame
{
    uint32_t offset;
    std::vector<uint8_t> image;

    Frame(uint32_t offset, std::vector<uint8_t> image) : offset(offset), image(image) {}

    uint16_t headerSize() const
    {
        return image[0] << 8 | image[1];
    }

    uint16_t width() const
    {
        return image[2] << 8 | image[3];
    }

    uint16_t height() const
    {
        return image[4] << 8 | image[5];
    }

    uint32_t size() const
    {
        return image.size();
    }
};

struct Clip
{
    uint32_t offset;
    uint32_t nextOffset;
    uint32_t frameCount;
    std::vector<uint32_t> frameOffsets;
    std::vector<Frame> frames;

    uint32_t headerSize() const
    {
        return sizeof(frameCount)
            + sizeof(uint32_t) * frames.size()
            + sizeof(nextOffset);
    }

    uint32_t size() const
    {
        assert(frameCount == frameOffsets.size());
        uint32_t frameSizes = 0;
        for(size_t i = 0; i < frames.size(); i++)
        {
            Frame frame = frames[i];
            assert(frameSizes + frame.size() > frameSizes);
            frameSizes += frame.size();
        }
        return headerSize() + frameSizes;
    }
};

class CLX
{
public:
    std::vector<uint32_t> groupOffsets;
    std::vector<Clip> clips;

    CLX(std::ifstream& stream)
    {
        groupOffsets = findGroupOffsets(stream);
        clips = findClips(stream, groupOffsets);
    }

    CLX(const std::string& filename)
    {
        std::ifstream stream(filename, std::ios::binary);
        groupOffsets = findGroupOffsets(stream);
        clips = findClips(stream, groupOffsets);

        assert(groupOffsets.size() == clips.size());
        for(size_t i = 0; i < clips.size(); i++)
        {
            Clip clip = clips[i];
            assert(clip.frameCount == clip.frames.size());
            for(size_t j = 0; j < clip.frameCount; j++)
            {
                Frame frame = clip.frames[j];
            }
        }
    }

    bool isMonoGroup() const
    {
        return groupOffsets.size() == 1;
    }

    uint32_t fileSize() const
    {
        return clips[clips.size() - 1].offset + clips[clips.size() - 1].nextOffset;
    }

    void removeGroup(size_t groupIndex)
    {
        assert(groupIndex < groupOffsets.size());
        groupOffsets.erase(groupOffsets.begin() + groupIndex);
        clips.erase(clips.begin() + groupIndex);
        recalculateOffsets();
    }

    void recalculateOffsets()
    {
        if(groupOffsets.size() == 1)
        {
            //monogroup, clip starts at offset 0
            clips[0].offset = 0;
            clips[0].nextOffset = clips[0].size();
        }
        else
        {
            //first clip comes after the group offset list
            clips[0].offset = groupOffsets.size() * sizeof(uint32_t);
            clips[0].nextOffset = clips[0].size();
        }
        for(size_t i = 1; i < clips.size(); i++)
        {
            //second clip comes after the first clip + its frames
            clips[i].offset = clips[i - 1].offset + clips[i - 1].size();
            clips[i].nextOffset = clips[i].size(); //nextOffset is relative to current offset
        }


        for(size_t i = 0; i < groupOffsets.size(); i++)
        {
            groupOffsets[i] = clips[i].offset;
        }

        //frames come after their clip header (clip offset + clip header size)
        //clips[0].frames[0].offset = clips[0].offset + clips[0].headerSize();
        size_t i = 0;
        for(Clip& clip: clips)
        {
            clip.frames[0].offset = clip.offset + clip.headerSize();
            clip.frameOffsets[0] = clip.frames[0].offset;
            for(size_t f = 1; f < clip.frames.size(); f++)
            {
                clip.frames[f].offset = clip.frames[f - 1].offset + clip.frames[f - 1].image.size();
                clip.frameOffsets[f] = clip.frames[f].offset;
            }

            i++;
        }
    }

    private:
    std::vector<uint32_t> findGroupOffsets(std::ifstream& stream)
    {
        std::vector<uint32_t> groupOffsets;
        uint32_t fileSize;

        stream.seekg(0, std::ios::end);
        fileSize = stream.tellg();
        stream.seekg(0, std::ios::beg);
        std::cout << "File size = " << fileSize << std::endl;

        uint32_t groupFrameCountOrFirstOffset = readLittleEndianDoubleWord(stream);
        stream.seekg(groupFrameCountOrFirstOffset * sizeof(uint32_t) + sizeof(uint32_t));

        uint32_t nextGroupOffset = readLittleEndianDoubleWord(stream);
        if(fileSize == nextGroupOffset) //monogroup
        {
            groupOffsets.push_back(0);
            std::cout << "Monogroup, adding artificial group offset of 0" << std::endl;
            return groupOffsets;
        }
        //groupFrameCountOrFirstOffset is the first group offset after all
        stream.seekg(0, std::ios::beg);
        while(stream.tellg() != groupFrameCountOrFirstOffset)
        {
            uint32_t groupOffset = readLittleEndianDoubleWord(stream);
            groupOffsets.push_back(groupOffset);
        }
        return groupOffsets;
    }

    std::vector<Frame> findFrames(std::ifstream& stream, const std::vector<uint32_t>& frameOffsets, uint32_t nextGroupOffset, bool isLast)
    {
        std::vector<Frame> frames;
        for(size_t f = 0; f < frameOffsets.size(); f++)
        {
            uint32_t startOffset = frameOffsets[f];
            bool isLast = f == frameOffsets.size() - 1;
            uint32_t endOffset = isLast ? nextGroupOffset : frameOffsets[f + 1];
            stream.seekg(startOffset, std::ios::beg);

            uint32_t frameSize = endOffset - startOffset;
            std::vector<uint8_t> image;
            image.resize(frameSize);
            stream.read(reinterpret_cast<char*>(image.data()), frameSize);
            Frame frame(startOffset, image);
            frames.push_back(frame);
        }
        return frames;
    }

    std::vector<Clip> findClips(std::ifstream& stream, const std::vector<uint32_t>& groupOffsets)
    {
        std::vector<Clip> clips;
        for(size_t i = 0; i < groupOffsets.size(); i++)
        {
            uint32_t groupOffset = groupOffsets[i];
            stream.seekg(groupOffset, std::ios::beg);
            Clip clip;
            clip.offset = groupOffset;
            clip.frameCount = readLittleEndianDoubleWord(stream);
            for(uint32_t i = 0; i < clip.frameCount; i++)
            {
                uint32_t frameOffset = readLittleEndianDoubleWord(stream) + groupOffset;
                clip.frameOffsets.push_back(frameOffset);
            }
            clip.nextOffset = readLittleEndianDoubleWord(stream);

            bool isLast = i == groupOffsets.size() - 1;
            clip.frames = findFrames(stream, clip.frameOffsets, groupOffset + clip.nextOffset, isLast);
            clips.push_back(clip);
        }
        return clips;
    }
};

void writeToFile(const CLX& clx, const std::string& path)
{
    std::ofstream stream(path, std::ios::binary);
    for(const uint32_t offset: clx.groupOffsets)
    {
        writeLittleEndianDoubleWord(stream, offset);
    }
    std::cout << "Wrote group offsets" << std::endl;

    for(const Clip& clip: clx.clips)
    {
        stream.seekp(clip.offset);
        writeLittleEndianDoubleWord(stream, clip.frameCount);
        for(uint32_t frameOffset: clip.frameOffsets)
        {
            writeLittleEndianDoubleWord(stream, frameOffset - clip.offset);
        }
        writeLittleEndianDoubleWord(stream, clip.nextOffset);
    }
    std::cout << "Wrote clip headers" << std::endl;

    for(const Clip& clip: clx.clips)
    {
        for(const Frame& frame: clip.frames)
        {
            stream.seekp(frame.offset);
            stream.write(reinterpret_cast<const char*>(frame.image.data()), frame.image.size());
        }
    }

    std::cout << "Wrote frames" << std::endl;
}

void removeGroups(std::string clxPath, const std::vector<size_t>& groupIndexes)
{
    CLX clx(clxPath);
    std::set<size_t, std::greater<size_t>> toRemove(groupIndexes.begin(), groupIndexes.end());
    std::cout << clx.groupOffsets.size() << " groups in file " << clxPath << std::endl;
    for(size_t groupIndex: toRemove)
    {
        std::cout << "Removing group #" << groupIndex << std::endl;
        clx.removeGroup(groupIndex);
    }
    std::cout << clx.groupOffsets.size() << " groups" << std::endl;
    writeToFile(clx, clxPath + ".stripped");

}

int main(int argc, char *argv[])
{
    if(argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " <path to clx> <space separated groups to remove>" << std::endl;
        return 0;
    }
    std::vector<size_t> toRemove;
    for(int i = 2; i < argc; i++)
    {
        toRemove.push_back(std::stoi(argv[i]));
    }
    removeGroups(argv[1], toRemove);
    return 0;
}
