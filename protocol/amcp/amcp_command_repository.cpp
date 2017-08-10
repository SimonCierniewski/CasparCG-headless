/*
* Copyright (c) 2011 Sveriges Television AB <info@casparcg.com>
*
* This file is part of CasparCG (www.casparcg.com).
*
* CasparCG is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* CasparCG is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with CasparCG. If not, see <http://www.gnu.org/licenses/>.
*
* Author: Helge Norberg, helge.norberg@svt.se
*/

#include "../StdAfx.h"

#include "amcp_command_repository.h"

#include <map>

namespace caspar { namespace protocol { namespace amcp {

AMCPCommand::ptr_type find_command(
		const std::map<std::wstring, std::pair<amcp_command_func, int>>& commands,
		const std::wstring& str,
		const command_context& ctx,
		std::list<std::wstring>& tokens)
{
	std::wstring subcommand;

	if (!tokens.empty())
		subcommand = boost::to_upper_copy(tokens.front());

	// Start with subcommand syntax like MIXER CLEAR etc
	if (!subcommand.empty())
	{
		auto s = str + L" " + subcommand;
		auto subcmd = commands.find(s);

		if (subcmd != commands.end())
		{
			tokens.pop_front();
			return std::make_shared<AMCPCommand>(ctx, subcmd->second.first, subcmd->second.second, s);
		}
	}

	// Resort to ordinary command
	auto s = str;
	auto command = commands.find(s);

	if (command != commands.end())
		return std::make_shared<AMCPCommand>(ctx, command->second.first, command->second.second, s);

	return nullptr;
}

struct amcp_command_repository::impl
{
	std::vector<channel_context>								channels;
	std::shared_ptr<core::thumbnail_generator>					thumb_gen;
	spl::shared_ptr<core::media_info_repository>				media_info_repo;
	spl::shared_ptr<core::system_info_provider_repository>		system_info_provider_repo;
	spl::shared_ptr<core::cg_producer_registry>					cg_registry;
	spl::shared_ptr<core::help_repository>						help_repo;
	spl::shared_ptr<const core::frame_producer_registry>		producer_registry;
	spl::shared_ptr<const core::frame_consumer_registry>		consumer_registry;
	std::shared_ptr<accelerator::ogl::device>					ogl_device;
	std::promise<bool>&											shutdown_server_now;

	std::map<std::wstring, std::pair<amcp_command_func, int>>	commands;
	std::map<std::wstring, std::pair<amcp_command_func, int>>	channel_commands;

	impl(
			const std::vector<spl::shared_ptr<core::video_channel>>& channels,
			const std::shared_ptr<core::thumbnail_generator>& thumb_gen,
			const spl::shared_ptr<core::media_info_repository>& media_info_repo,
			const spl::shared_ptr<core::system_info_provider_repository>& system_info_provider_repo,
			const spl::shared_ptr<core::cg_producer_registry>& cg_registry,
			const spl::shared_ptr<core::help_repository>& help_repo,
			const spl::shared_ptr<const core::frame_producer_registry>& producer_registry,
			const spl::shared_ptr<const core::frame_consumer_registry>& consumer_registry,
			const std::shared_ptr<accelerator::ogl::device>& ogl_device,
			std::promise<bool>& shutdown_server_now)
		: thumb_gen(thumb_gen)
		, media_info_repo(media_info_repo)
		, system_info_provider_repo(system_info_provider_repo)
		, cg_registry(cg_registry)
		, help_repo(help_repo)
		, producer_registry(producer_registry)
		, consumer_registry(consumer_registry)
		, ogl_device(ogl_device)
		, shutdown_server_now(shutdown_server_now)
	{
		int index = 0;
		for (const auto& channel : channels)
		{
			std::wstring lifecycle_key = L"lock" + boost::lexical_cast<std::wstring>(index);
			this->channels.push_back(channel_context(channel, lifecycle_key));
			++index;
		}
	}
};

amcp_command_repository::amcp_command_repository(
		const std::vector<spl::shared_ptr<core::video_channel>>& channels,
		const std::shared_ptr<core::thumbnail_generator>& thumb_gen,
		const spl::shared_ptr<core::media_info_repository>& media_info_repo,
		const spl::shared_ptr<core::system_info_provider_repository>& system_info_provider_repo,
		const spl::shared_ptr<core::cg_producer_registry>& cg_registry,
		const spl::shared_ptr<core::help_repository>& help_repo,
		const spl::shared_ptr<const core::frame_producer_registry>& producer_registry,
		const spl::shared_ptr<const core::frame_consumer_registry>& consumer_registry,
		const std::shared_ptr<accelerator::ogl::device>& ogl_device,
		std::promise<bool>& shutdown_server_now)
		: impl_(new impl(
				channels,
				thumb_gen,
				media_info_repo,
				system_info_provider_repo,
				cg_registry,
				help_repo,
				producer_registry,
				consumer_registry,
				ogl_device,
				shutdown_server_now))
{
}

AMCPCommand::ptr_type amcp_command_repository::create_command(const std::wstring& s, IO::ClientInfoPtr client, std::list<std::wstring>& tokens) const
{
	auto& self = *impl_;

	command_context ctx(
			std::move(client),
			channel_context(),
			-1,
			-1,
			self.channels,
			self.help_repo,
			self.media_info_repo,
			self.cg_registry,
			self.system_info_provider_repo,
			self.thumb_gen,
			self.producer_registry,
			self.consumer_registry,
			self.ogl_device,
			self.shutdown_server_now);

	auto command = find_command(self.commands, s, ctx, tokens);

	if (command)
		return command;

	return nullptr;
}

const std::vector<channel_context>& amcp_command_repository::channels() const
{
	return impl_->channels;
}

AMCPCommand::ptr_type amcp_command_repository::create_channel_command(
		const std::wstring& s,
		IO::ClientInfoPtr client,
		unsigned int channel_index,
		int layer_index,
		std::list<std::wstring>& tokens) const
{
	auto& self = *impl_;

	auto channel = self.channels.at(channel_index);

	command_context ctx(
			std::move(client),
			channel,
			channel_index,
			layer_index,
			self.channels,
			self.help_repo,
			self.media_info_repo,
			self.cg_registry,
			self.system_info_provider_repo,
			self.thumb_gen,
			self.producer_registry,
			self.consumer_registry,
			self.ogl_device,
			self.shutdown_server_now);

	auto command = find_command(self.channel_commands, s, ctx, tokens);

	if (command)
		return command;

	return nullptr;
}

void amcp_command_repository::register_command(
		std::wstring category,
		std::wstring name,
		core::help_item_describer describer,
		amcp_command_func command,
		int min_num_params)
{
	auto& self = *impl_;
	self.help_repo->register_item({ L"AMCP", category }, name, describer);
	self.commands.insert(std::make_pair(std::move(name), std::make_pair(std::move(command), min_num_params)));
}

void amcp_command_repository::register_channel_command(
		std::wstring category,
		std::wstring name,
		core::help_item_describer describer,
		amcp_command_func command,
		int min_num_params)
{
	auto& self = *impl_;
	self.help_repo->register_item({ L"AMCP", category }, name, describer);
	self.channel_commands.insert(std::make_pair(std::move(name), std::make_pair(std::move(command), min_num_params)));
}

spl::shared_ptr<core::help_repository> amcp_command_repository::help_repo() const
{
	return impl_->help_repo;
}

}}}
